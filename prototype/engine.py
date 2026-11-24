"""Prototype of the real-time modal drum engine (block-based, per-drum resonator bank).
One bank per drum: re-strikes add into the existing mode states (a struck membrane is linear
superposition) so polyphony per drum is free. Tension-modulation glide is driven by the bank's own
(f^2-weighted) energy, so it scales with velocity and accumulates in rolls."""
import json, os, numpy as np
from scipy.signal import butter, sosfilt, lfilter
from scipy.special import jn, jn_zeros
SR=44100; BLK=32
# fixed one-third-octave bands of the modelled contact noise burst (180 Hz up)
BURST_BANDS=[(180*2**(k/3),180*2**((k+1)/3)) for k in range(24) if 180*2**(k/3)<19000]
# Native sizes from the 1996 TAMA catalog (Simon Phillips Starclassic Maple kit). Best-effort reading of a low-res scan.
NATIVE_IN={'KICK':22.0,'SNRE':14.0,'TOM_729':10.0,'TOM_730':12.0,'TOM_731':13.0,'TOM_732':14.0,'TOM_733':16.0,'TOM_734':18.0,
           'TOM_735':22.0,'TOM_736':6.0,'TOM_737':6.0,'TOM_738':6.0,'TOM_739':6.0}
OCTOBANS=('TOM_736','TOM_737','TOM_738','TOM_739')
NATIVE_STRIKE=0.35   # assumed strike radius (fraction of head radius) of the original recordings - not measured
# Parameter table. 'exposed' is left undecided (None): which ones reach the plugin UI is Kris's call.
PARAMS={
 'tune_semi':   dict(default=0.0, lo=-2, hi=2,    unit='semitones', when='live',   exposed=None, doc='head tension: scales every mode'),
 'decay':       dict(default=1.0, lo=0.25,hi=4,   unit='x ring time',when='live', exposed=None, doc='ring-time multiplier with built-in HF tilt'),
 'glide_amt':   dict(default=1.0, lo=0, hi=3,     unit='x',         when='strike', exposed=None, doc='tension-modulation pitch drop depth'),
 'click_mix':   dict(default=1.0, lo=0, hi=2,     unit='x',         when='strike', exposed=None, doc='20 ms contact transient level'),
 'noise_mix':   dict(default=1.0, lo=0, hi=2,     unit='x',         when='strike', exposed=None, doc='noise layer (wires / dense modes) level'),
 'vel_range_db':dict(default=30.0,lo=0, hi=48,    unit='dB',        when='strike', exposed=None, doc='level span from vel 0 to vel 1'),
 'strike_pos':  dict(default=NATIVE_STRIKE, lo=0.0, hi=0.95, unit='r/a', when='strike', exposed=None, doc='0 = centre, 1 = rim; reshapes modes by membrane mode shape J_m(j_mn r). Not on bass drums (fixed beater position, as sampled)'),
 'snare_buzz':  dict(default=0.5, lo=0, hi=1,     unit='',          when='live',   exposed=None, doc='KIT level: sympathetic snare buzz, how strongly the other drums set the snare heads and wires going (0 = off)'),
 'lug_detune':  dict(default=0.0, lo=0, hi=1,     unit='',          when='live',   exposed=None, doc='PER DRUM: uneven lug tension (0 = evenly tuned, as now; 1 = 5 % RMS tension error between lugs). Fundamental stays in tune; overtone pairs split and beat. Pitch shift is live; the split of the excitation applies from the next hit'),
 'size_in':     dict(default=None, lo=None, hi=None, unit='inch',   when='live',   exposed=None, doc='head diameter: f ~ 1/D, decay rates ~ 1/D (constant Q), noise bands shift; default = catalog size, range 0.5x..2x'),
 'tube_len':    dict(default=1.0, lo=0.5, hi=2.0, unit='x length',  when='live',   exposed=None, doc='octobans only: scales the air-column (main pitch) modes ~ 1/length'),
}
class ModalDrum:
    def __init__(self, path, restrike_damp=True, variation=True, lug_detune=0.0, tune_semi=0.0, decay=1.0, glide_amt=1.0, click_mix=1.0, noise_mix=1.0, use_click=True, vel_range_db=30.0, glide_w_exp=2.0,
                 strike_pos=None, size_in=None, tube_len=1.0):
        m=json.load(open(path)); self.m=m
        self.path=path; name=os.path.basename(path); self.key=next((k for k in NATIVE_IN if name.startswith(k)),None)
        self.arts=m.get('articulations'); self.art='head'
        L=m['layers'] if not self.arts else self.arts['head']['layers']; self.nl=len(L)
        self.LF=np.array([l['freq'] for l in L]); self.LD=np.array([l['decay'] for l in L])
        self.F=self.LF[0].copy(); self.D=self.LD[0].copy(); self.tau=m.get('glide_tau',0.05); self.gam=0.0
        self.A=np.array([l['amp'] for l in L]); self.P=np.array([l['phase'] for l in L])
        self.G=np.array([l['glide_g'] for l in L]); self.T=np.array([l.get('glide_tau',m.get('glide_tau',0.05)) for l in L])
        self.GW=np.array([l['glide_w'] for l in L]) if 'glide_w' in L[0] else None; self.gw=None
        self.G2=np.array([l.get('glide_g2',0.0) for l in L]); self.T2=np.array([l.get('glide_tau2',1.0) for l in L]); self.gam2=0.0; self.tau2=1.0
        self.TR=[l.get('transient') for l in L]   # modelled contact transient per layer (no recorded audio)
        nb=L[0]['noise']; self.bands=[(b['lo'],b['hi']) for b in nb]
        self.NPk=np.array([[b['peak_rms'] for b in l['noise']] for l in L])
        self.NDk=np.array([[b['decay'] for b in l['noise']] for l in L])
        self.sos=[butter(2,[lo,min(hi,21000)],'band',fs=SR,output='sos') for lo,hi in self.bands]
        _r=np.random.default_rng(0).standard_normal(1<<16)
        self.bnorm=np.array([1/np.sqrt(np.mean(sosfilt(s_,_r)[2000:]**2)) for s_ in self.sos])
        self.tune=2**(float(np.clip(tune_semi,-2,2))/12); self.gamt=glide_amt   # Tune range +-2 st (Kris, 2026-11-24)
        # Decay: ring-time multiplier (0.25..4, 1 = as sampled). Low modes get the full factor, highs less
        # (w: 1 up to 2x the main mode, -0.35 per octave above, floor 0.3) so long settings don't go metallic.
        _E=self.A[-1]**2/np.maximum(self.LD[-1],.5); self.fref=2*min(self.LF[-1][np.argmax(_E)],400.0)
        self.bandfc=np.array([np.sqrt(lo*hi) for lo,hi in self.bands])
        self.set_decay(decay)
        self.restrike_damp=restrike_damp; self.variation=variation; self.lug=float(np.clip(lug_detune,0,1))
        import zlib; self.vrng=np.random.default_rng(zlib.crc32(path.split('/')[-1].encode()))
        self.zrm=None; self.rm_left=0; self.rm_tot=1
        self.cmix=click_mix; self.nmix=noise_mix; self.use_click=use_click; self.vrange=vel_range_db
        # glide calibration: hardest layer at full velocity defines reference energy
        self.Gs=self.G; self.gmax=1.3*self.G[0]+1e-6
        if self.arts: self._art_tables={a:self._tables(v['layers']) for a,v in self.arts.items()}
        self.click_gain={a:float(v.get('click_gain',0.0)) for a,v in self.arts.items()} if self.arts else {}
        self.transients=[]   # modelled contact clicks (short HF modes), rendered outside the main bank
        self.z=np.zeros(len(self.F),complex); self.env=np.zeros(len(self.bands)); self.nd=self.NDk[-1].copy()
        self.zi=[np.zeros((2,2)) for _ in self.bands]; self.rng=np.random.default_rng(7)
        self.pulses=[]    # modelled contact pulses: [t0_s, width_s, amp, pos]
        self.bursts=[]    # modelled contact noise bursts: [pk(nb), tp(nb), d(nb), pos]
        self.bsos=[butter(2,[lo,min(hi,20000)],'band',fs=SR,output='sos') for lo,hi in BURST_BANDS]
        _rb=np.random.default_rng(0).standard_normal(1<<16)
        self.bbnorm=np.array([1/np.sqrt(np.mean(sosfilt(s_,_rb)[2000:]**2)) for s_ in self.bsos])
        self.bzi=[np.zeros((2,2)) for _ in BURST_BANDS]; self.brng=np.random.default_rng(11)
        self._label_modes()
        self._lug_setup()
        self.D0=NATIVE_IN.get(self.key,14.0); self.is_octoban=self.key in OCTOBANS
        self.is_kick=(self.key=='KICK')     # bass drums: fixed beater position (as sampled), no strike_pos
        self.r0=NATIVE_STRIKE; self.strike_pos=self.r0 if (strike_pos is None or self.is_kick) else float(np.clip(strike_pos,0,0.95))
        self.sf=1.0; self.size_in=self.D0
        if size_in is not None: self.set_size(size_in)
        self.tubefac=np.ones(len(self.F)); self.tube_len=1.0; self.set_tube_len(tube_len)
    # ---------------- mode shapes / strike position ----------------
    def _label_modes(self):
        """Strike-position model.
        - main-mode cluster (<=1.10x main): softened physical drop toward the rim (sqrt of J0 ratio, max -6 dB);
          total modal energy is renormalised so position changes timbre, not level
        - low modes (1.10x .. 3.6x main): nearest ideal circular-membrane mode (m,n), shape J_m(j_mn r)
        - dense region (>3.6x main): mode identity is meaningless, use the RMS shape of all membrane modes within +-8 %
          of that frequency ratio (a smooth 'brightness vs radius' curve)"""
        mid=self.nl//2; F=self.LF[mid]; E=self.A[mid]**2/np.maximum(self.LD[mid],.5)
        low=F<600; self.f_main=F[low][np.argmax(E[low])] if low.any() else F[np.argmax(E)]
        tab=sorted(((mm,n,jn_zeros(mm,10)[n-1]) for mm in range(0,40) for n in range(1,11)),key=lambda t:t[2])
        j01=tab[0][2]; self._tab_m=np.array([t[0] for t in tab]); self._tab_j=np.array([t[2] for t in tab]); rat=self._tab_j/j01
        self.mode_kind=np.zeros(len(F),int)          # 0 main, 1 labelled low mode, 2 dense
        self.mode_m=np.zeros(len(F),int); self.mode_j=np.zeros(len(F)); self.dense_idx=[None]*len(F)
        for k,f in enumerate(F):
            r=f/self.f_main
            if r<1.10: self.mode_kind[k]=0
            elif r<3.6:
                i=int(np.argmin(np.abs(np.log(rat[1:]/r))))+1
                self.mode_kind[k]=1; self.mode_m[k]=tab[i][0]; self.mode_j[k]=tab[i][2]
            else:
                self.mode_kind[k]=2; self.dense_idx[k]=np.where(np.abs(np.log(rat/r))<np.log(1.08))[0]
    LUGS={'TOM_729':6,'TOM_730':6,'TOM_731':6,'TOM_732':6,'TOM_733':8,'TOM_734':8,'TOM_735':10,'KICK':10,'SNRE':10,
          'TOM_736':4,'TOM_737':4,'TOM_738':4,'TOM_739':4}     # lugs per head (assumed typical counts)
    def _lug_setup(self):
        """Uneven lug tension (lug_detune 0..1, 0 = evenly tuned, as sampled). The head's tension varies around the rim,
        with the same mean (the fundamental stays in tune). A tension ripple at angular harmonic 2m splits each degenerate
        pair of m-diametral modes into two eigenmodes a little above and below the even-tension frequency
        (first-order membrane perturbation: df/f = +-|eps_2m|/4). The strike excites the two in proportion to cos/sin of
        m x (strike angle - eigen-angle), so the overtones beat and clash slightly; m = 0 modes (incl. the fundamental)
        do not move. Only the strongest split slots (up to 40) get a twin oscillator."""
        import zlib
        rng=np.random.default_rng(zlib.crc32(('lugs'+os.path.basename(self.path)).encode()))
        N=next((v for k,v in self.LUGS.items() if os.path.basename(self.path).startswith(k)),8)
        e=rng.standard_normal(N); e-=e.mean(); e/=np.sqrt(np.mean(e**2))+1e-12      # unit-RMS tension error per lug, zero mean
        th=2*np.pi*np.arange(N)/N
        K=len(self.mode_kind); m=np.zeros(K,int)
        m[self.mode_kind==1]=self.mode_m[self.mode_kind==1]
        for k in np.where(self.mode_kind==2)[0]:
            ix=self.dense_idx[k]
            if ix is not None and len(ix): m[k]=int(self._tab_m[ix][rng.integers(len(ix))])
        def eps_hat(kk):      # complex Fourier coefficient of the tension error at angular harmonic kk, smoothed between lugs
            return (2.0/N)*np.sum(e*np.exp(-1j*kk*th))*np.exp(-(kk/(1.5*N))**2)
        self.lug_m=m; self.lug_rel=np.zeros(K); self.lug_phi=np.zeros(K)
        for k in np.where(m>0)[0]:
            c=eps_hat(2*m[k]); self.lug_rel[k]=np.abs(c)/4; self.lug_phi[k]=np.angle(c)/(2*m[k])
        mid=self.nl//2; E=self.A[mid]**2/np.maximum(self.LD[mid],.5)*(m>0)*(self.lug_rel>1e-3)
        self.lugS=np.array([k for k in np.argsort(-E)[:40] if E[k]>0],int)
        self.lug_theta=rng.uniform(0,2*np.pi)              # where the stick lands relative to the lugs
        self.zt=np.zeros(len(self.lugS),complex); self.zrmt=None
        self.LUG_SIGMA=0.05                                 # lug_detune 1 = 5 % RMS tension error between lugs
    def _lug_delta(self):
        d=np.zeros(len(self.mode_kind)); d[self.lugS]=self.lug*self.LUG_SIGMA*self.lug_rel[self.lugS]; return d
    def _strike_gain(self):
        if self.is_kick or self.strike_pos==self.r0: return 1.0
        r,r0=self.strike_pos,self.r0; g=np.ones(len(self.mode_kind))
        lo=self.mode_kind==1
        p0=jn(self.mode_m[lo],self.mode_j[lo]*r0); p=jn(self.mode_m[lo],self.mode_j[lo]*r); eps=0.15*np.where(p0>=0,1.0,-1.0)
        g[lo]=np.clip((p+eps)/(p0+eps),-2.0,2.0)
        # main mode: physical drop toward the rim, but softened (square root of the ideal J0 ratio, floor -6 dB)
        mm=self.mode_kind==0; j=2.4048
        g[mm]=np.clip(np.sqrt(max(jn(0,j*r),0.0)/jn(0,j*r0)),0.5,1.25)
        for k in np.where(self.mode_kind==2)[0]:
            ix=self.dense_idx[k]
            if ix is None or len(ix)==0: continue
            a=np.sqrt(np.mean(jn(self._tab_m[ix],self._tab_j[ix]*r)**2)); b=np.sqrt(np.mean(jn(self._tab_m[ix],self._tab_j[ix]*r0)**2))
            g[k]=np.clip((a+0.02)/(b+0.02),0.5,2.0)
        # keep the hit's total modal energy equal to the as-sampled strike, so position changes timbre, not level
        mid=self.nl//2; e=self.A[mid]**2/np.maximum(self.LD[mid],.5)
        # keep the main-mode cluster at least 6 dB above any other single slot's sustained energy (A^2/2d),
        # so a rim strike can brighten the drum but never hand the pitch to an overtone
        if mm.any() and (~mm).any():
            A=self.A[mid]; D=self.LD[mid]
            # check amplitudes over the main mode's whole ring (up to its T60): main must stay >= 2x any other slot
            tt=np.linspace(0,6.91/np.median(D[mm]),24)[:,None]
            def margin(gg):
                am=np.abs(np.sum((A[mm]*gg[mm])[None,:]*np.exp(-D[mm][None,:]*tt),1))
                ao=np.max(np.abs(A[~mm]*gg[~mm])[None,:]*np.exp(-D[~mm][None,:]*tt),1)
                return np.min(am/(2.0*np.maximum(ao,1e-12)))
            # relative rule: a strike position may not bring any overtone closer to the main mode than the
            # as-sampled hit already does (and never closer than 6 dB if the as-sampled hit is clear of that)
            lim=min(margin(np.ones_like(g)),1.0); k=margin(g)
            if k<lim: g[~mm]*=k/lim
        return g*np.sqrt(e.sum()/np.sum(e*g**2))
    # ---------------- size / tube ----------------
    def set_size(self,size_in):
        d=float(np.clip(size_in,0.5*self.D0,2.0*self.D0)); self.size_in=d; self.sf=self.D0/d
        edges=[(lo*self.sf,min(hi*self.sf,21000)) for lo,hi in self.bands]
        self.sos=[butter(2,[max(lo,20),max(hi,lo*1.05+1)],'band',fs=SR,output='sos') for lo,hi in edges]
        _r=np.random.default_rng(0).standard_normal(1<<16)
        self.bnorm=np.array([1/np.sqrt(np.mean(sosfilt(s_,_r)[2000:]**2)) for s_ in self.sos])
    def set_tube_len(self,x):
        self.tube_len=float(np.clip(x,0.5,2.0))
        if self.is_octoban:
            cl=np.abs(np.log(self.LF[self.nl//2]/self.f_main))<np.log(1.10)
            self.tubefac=np.where(cl,1.0/self.tube_len,1.0)
    def set(self,name,val):
        if name=='tune_semi': self.tune=2**(float(np.clip(val,-2,2))/12)
        elif name=='decay': self.set_decay(val)
        elif name=='glide_amt': self.gamt=val
        elif name=='click_mix': self.cmix=val
        elif name=='noise_mix': self.nmix=val
        elif name=='vel_range_db': self.vrange=val
        elif name=='strike_pos':
            if not self.is_kick: self.strike_pos=float(np.clip(val,0,0.95))
        elif name=='lug_detune': self.lug=float(np.clip(val,0,1))
        elif name=='size_in': self.set_size(val)
        elif name=='tube_len': self.set_tube_len(val)
        else: raise KeyError(name)
    def _tables(self,L):
        """per-articulation layer tables (same shapes as the single-articulation fields)"""
        return dict(nl=len(L),LF=np.array([l['freq'] for l in L]),LD=np.array([l['decay'] for l in L]),
            A=np.array([l['amp'] for l in L]),P=np.array([l['phase'] for l in L]),G=np.array([l['glide_g'] for l in L]),
            T=np.array([l.get('glide_tau',0.05) for l in L]),G2=np.array([l.get('glide_g2',0.0) for l in L]),
            T2=np.array([l.get('glide_tau2',1.0) for l in L]),GW=(np.array([l['glide_w'] for l in L]) if 'glide_w' in L[0] else None),
            TR=[l.get('transient') for l in L],NPk=np.array([[b['peak_rms'] for b in l['noise']] for l in L]),
            NDk=np.array([[b['decay'] for b in l['noise']] for l in L]),
            CM=[np.array(l['click_modes']) if 'click_modes' in l else None for l in L])
    def _use_art(self,art):
        if not self.arts or art==self.art: return
        t=self._art_tables[art]; self.art=art
        self.nl=t['nl']; self.LF=t['LF']; self.LD=t['LD']; self.A=t['A']; self.P=t['P']; self.G=t['G']; self.Gs=t['G']
        self.T=t['T']; self.G2=t['G2']; self.T2=t['T2']; self.GW=t['GW']; self.TR=t['TR']; self.NPk=t['NPk']; self.NDk=t['NDk']
    def _w(self,f): return np.clip(1-0.35*np.log2(np.maximum(f,1)/self.fref),0.3,1.0)
    def set_decay(self,decay):
        """live-adjustable: takes effect on ringing modes at the next 32-sample block"""
        self.decay=float(np.clip(decay,0.25,4.0))
    def _dmul(self,f): return self.decay**(-self._w(f))   # multiplies decay *rates*
    def _shape2(self,r):
        """normalised squared mode displacement at radius r (0 centre .. 1 rim), per slot"""
        rr=np.linspace(0,1,200); w=np.ones(len(self.mode_kind))
        mm=self.mode_kind==0; w[mm]=jn(0,2.4048*r)**2
        lo=np.where(self.mode_kind==1)[0]
        for k in lo:
            m_,j_=self.mode_m[k],self.mode_j[k]; w[k]=jn(m_,j_*r)**2/np.max(jn(m_,j_*rr)**2)
        for k in np.where(self.mode_kind==2)[0]:
            ix=self.dense_idx[k]
            if ix is None or len(ix)==0: w[k]=0.3; continue
            w[k]=np.mean([jn(self._tab_m[q],self._tab_j[q]*r)**2/np.max(jn(self._tab_m[q],self._tab_j[q]*rr)**2) for q in ix])
        return np.clip(w,0,1)
    def _contact_damp(self,vel,art):
        """Re-strike damping: while the stick (beater) is on the head it loads each mode in proportion to that mode's
        motion at the contact point; modes whose period is shorter than the contact time lose most. Applies only to what
        is already ringing (the new hit's own amplitudes already contain its contact)."""
        if self.is_kick: r=0.05; tc=4.5e-3*(vel+0.1)**-0.2      # felt beater near the centre, longer contact
        elif art=='rim': r=0.9; tc=1.5e-3*(vel+0.1)**-0.2       # stick lands near the hoop
        elif art=='side': r=0.55; tc=5e-3                       # side stick: stick (and hand) lie across the head
        else: r=self.strike_pos; tc=2.0e-3*(vel+0.1)**-0.2
        key=(round(r,3),art)
        if getattr(self,'_shape_cache',{}).get(key) is None:
            self._shape_cache=getattr(self,'_shape_cache',{}); self._shape_cache[key]=self._shape2(r)
        s2=self._shape_cache[key]
        expo=(0.4+0.6*vel)*s2*(0.2+0.74*np.minimum(self.F*self.tune*tc,3.0))
        return np.exp(-expo), tc
    def _interp(self,vel):
        pos=(1-vel)*(self.nl-1); i=int(np.floor(pos)); i2=min(i+1,self.nl-1); fr=pos-i
        return i,i2,fr
    def strike(self,vel,art='head'):
        self._use_art(art)
        vel=float(np.clip(vel,0.0,1.0))
        if self.restrike_damp and np.any(self.z!=0):
            a_,tc_=self._contact_damp(vel,art)
            self.zrm=(self.zrm if self.zrm is not None and self.rm_left>0 else 0)+self.z*(1-a_)
            if self.lug>0 and len(self.lugS): self.zrmt=(self.zrmt if self.zrmt is not None and self.rm_left>0 else 0)+self.zt*(1-a_[self.lugS])
            self.rm_tot=max(1,int(tc_*SR)); self.rm_left=self.rm_tot
            self.env*=0.5+0.5*(1-0.5*vel)           # wires re-seat under the new hit
            # the previous hits' short contact modes are loaded by the stick too (treated like dense head modes)
            for tr in self.transients:
                f_=tr[0][:,0]*self.tune; fac=np.exp(-(0.4+0.6*vel)*0.3*(0.2+0.74*np.minimum(f_*tc_,3.0)))
                tr.append((tr[2],max(1,int(tc_*SR)),fac))
        tilt=0.0; pos_save=self.strike_pos
        if self.variation:
            # hit-to-hit variation: exact force, contact point and stick/beater contact stiffness differ a little each hit
            vel=float(np.clip(vel+self.vrng.normal(0,0.02),0.0,1.0))
            tilt=self.vrng.normal(0,0.4)             # dB per octave above 2x the main mode
            if not self.is_kick and art=='head':
                self.strike_pos=float(np.clip(pos_save+self.vrng.normal(0,0.015),0.0,0.95))
        i,i2,fr=self._interp(vel)
        gain=10**(-self.vrange*(1-vel)**1.3/20)
        amp=np.exp((1-fr)*np.log(self.A[i]+1e-9)+fr*np.log(self.A[i2]+1e-9))
        ph=self.P[i] if fr<0.5 else self.P[i2]
        sg=self._strike_gain() if art!='side' else 1.0; self.strike_pos=pos_save   # strike position has no meaning for a side stick
        if tilt: amp=amp*10**(tilt*np.log2(np.maximum(self.LF[i]/(2*self.f_main),1.0))/20)
        b_=gain*amp*sg*np.exp(1j*ph)
        if self.lug>0 and len(self.lugS):
            th_=self.lug_theta+(self.vrng.normal(0,0.15) if self.variation else 0.0)
            S=self.lugS; mm_=self.lug_m[S]; dl=mm_*(th_-self.lug_phi[S])
            self.zt+=b_[S]*np.sin(dl); b_=b_.copy(); b_[S]=b_[S]*np.cos(dl)
        self.z+=b_
        self.F=(1-fr)*self.LF[i]+fr*self.LF[i2]; self.D=(1-fr)*self.LD[i]+fr*self.LD[i2]
        self.gw=(1-fr)*self.GW[i]+fr*self.GW[i2] if self.GW is not None else None
        gt=((1-fr)*self.Gs[i]+fr*self.Gs[i2])*self.gamt; self.tau=(1-fr)*self.T[i]+fr*self.T[i2]
        # glide accumulates on re-strikes (roll), capped; scaled by how loud this hit is vs a full hit
        self.gam=min(self.gam+gt, self.gmax*self.gamt) if gt>0 else self.gam
        # slow second glide stage (gong drum): same accumulate-and-cap logic
        gt2=((1-fr)*self.G2[i]+fr*self.G2[i2])*self.gamt; self.tau2=(1-fr)*self.T2[i]+fr*self.T2[i2]
        if gt2>0: self.gam2=min(self.gam2+gt2, 1.3*max(self.G2[0],1e-6)*self.gamt)
        npk=np.exp((1-fr)*np.log(self.NPk[i]+1e-12)+fr*np.log(self.NPk[i2]+1e-12))
        self.env+=gain*npk*self.nmix; self.nd=(1-fr)*self.NDk[i]+fr*self.NDk[i2]
        if self.use_click and self.cmix>0:
            # modelled contact transient: damped modes + contact pulses (both layers, crossfaded) + one noise burst
            for w_,j_ in ((1-fr,i),(fr,i2)):
                tr=self.TR[j_]
                if w_<1e-6 or not tr: continue
                if tr.get('modes'): self.transients.append([np.array(tr['modes']),gain*self.cmix*w_,0])
                for t0_,wd_,a_ in tr.get('pulses',[]): self.pulses.append([t0_,wd_,gain*self.cmix*w_*a_,0])
            tri,tri2=self.TR[i],self.TR[i2]
            if tri and tri2 and tri.get('burst') and tri2.get('burst'):
                B1=np.array(tri['burst']); B2=np.array(tri2['burst'])
                pk=np.exp((1-fr)*np.log(B1[:,0]+1e-12)+fr*np.log(B2[:,0]+1e-12))
                self.bursts.append([gain*self.cmix*pk,(1-fr)*B1[:,1]+fr*B2[:,1],(1-fr)*B1[:,2]+fr*B2[:,2],0])
        # modelled rim click (hoop resonances), added on top of the sampled-residual click; 0 = as sampled
        if self.arts and self.click_gain.get(art,0)>0:
            cm=self._art_tables[art]['CM'][i if fr<0.5 else i2]
            if cm is not None: self.transients.append([cm,gain*self.click_gain[art],0])
    def render(self,n):
        out=np.zeros(n); k=0
        while k<n:
            b=min(BLK,n-k)
            gam=self.gam+self.gam2 if self.gam2 else self.gam
            self.gam*=np.exp(-b/(self.tau*SR))
            if self.gam2: self.gam2*=np.exp(-b/(self.tau2*SR))
            if self.gw is not None:
                p=np.exp((-self.D*self._dmul(self.F)*self.sf+2j*np.pi*self.F*self.tune*self.sf*self.tubefac*(1+gam*self.gw))/SR)
            elif self.sf==1.0 and self.tube_len==1.0:
                p=np.exp((-self.D*self._dmul(self.F)+2j*np.pi*self.F*self.tune*(1+gam))/SR)
            else:
                p=np.exp((-self.D*self._dmul(self.F)*self.sf+2j*np.pi*self.F*self.tune*self.sf*self.tubefac*(1+gam))/SR)
            if self.lug>0 and len(self.lugS):
                dl=self._lug_delta(); S=self.lugS; ph_=np.angle(p)
                pt=np.abs(p[S])*np.exp(1j*ph_[S]*(1-dl[S]))     # twin: the lower eigenmode
                p=np.abs(p)*np.exp(1j*ph_*(1+dl))                 # main slot: the upper eigenmode
                pwt=pt[:,None]**np.arange(1,b+1)[None,:]
                if self.rm_left>0 and self.zrmt is not None:
                    self.zt=self.zt-self.zrmt*(min(b,self.rm_left)/self.rm_tot); self.zrmt=self.zrmt*pwt[:,-1]
                zbt=self.zt[:,None]*pwt; out[k:k+b]+=np.real(zbt).sum(0); self.zt=zbt[:,-1]
            pw=p[:,None]**np.arange(1,b+1)[None,:]
            if self.rm_left>0:
                fr_=min(b,self.rm_left)/self.rm_tot; self.z=self.z-self.zrm*fr_; self.zrm=self.zrm*pw[:,-1]; self.rm_left-=b
            zb=self.z[:,None]*pw
            out[k:k+b]+=np.real(zb).sum(0)
            self.z=zb[:,-1]
            k+=b
        # noise bands (envelope per band, exp decay)
        t=np.arange(n); bmul=self._dmul(self.bandfc)
        for j,sos in enumerate(self.sos):
            if self.env[j]<1e-7: continue
            e=self.env[j]*np.exp(-self.nd[j]*bmul[j]*t/SR)
            nz,self.zi[j]=sosfilt(sos,self.rng.standard_normal(n),zi=self.zi[j])
            out+=nz*e*self.bnorm[j]
            self.env[j]=e[-1]*np.exp(-self.nd[j]*bmul[j]/SR)
        for tr in self.transients:
            cm,g_,pos=tr[0],tr[1],tr[2]; t=(pos+np.arange(n))/SR; idx=pos+np.arange(n)
            ramps=[(np.clip((idx-p0)/ns,0,1),fac) for p0,ns,fac in tr[3:]]
            for q,(f_,d_,a_,p_) in enumerate(cm):
                y_=g_*a_*np.exp(-d_*t)*np.cos(2*np.pi*f_*self.tune*t+p_)
                for rmp,fac in ramps: y_=y_*(1-rmp*(1-fac[q]))
                out+=y_
            tr[2]+=n
        self.transients=[tr for tr in self.transients if tr[2]<0.25*SR]
        for pu in self.pulses:
            # contact pulse: Ricker wavelet (zero mean) centred at tc with peak frequency fp
            tc_,fp_,a_,pos=pu; t=(pos+np.arange(n))/SR-tc_; u=(np.pi*fp_*t)**2
            out+=a_*(1-2*u)*np.exp(-u)
            pu[3]+=n
        self.pulses=[pu for pu in self.pulses if pu[3]/SR<pu[0]+3.0/pu[1]]
        if self.bursts:
            envs=np.zeros((len(BURST_BANDS),n))
            for bu in self.bursts:
                pk,tp,dd,pos=bu; t=(pos+np.arange(n))/SR
                envs+=pk[:,None]*np.where(t[None,:]<tp[:,None],t[None,:]/np.maximum(tp[:,None],1e-4),np.exp(-dd[:,None]*np.maximum(t[None,:]-tp[:,None],0)))
                bu[3]+=n
            for j,sos in enumerate(self.bsos):
                if envs[j].max()<1e-9: continue
                nz,self.bzi[j]=sosfilt(sos,self.brng.standard_normal(n),zi=self.bzi[j])
                out+=nz*envs[j]*self.bbnorm[j]
            self.bursts=[bu for bu in self.bursts if bu[3]<0.12*SR]
        return out
def sequence(drums, events, dur, sr=SR):
    """events: list of (time_s, drum_name, vel). renders by splitting at event times."""
    N=int(dur*sr); out=np.zeros(N)
    ev=sorted(events); cur=0
    times=sorted(set([int(t*sr) for t,_,_ in ev]+[N]))
    ei=0
    for T in times:
        if T>cur:
            for d in drums.values(): out[cur:T]+=d.render(T-cur)
            cur=T
        while ei<len(ev) and int(ev[ei][0]*sr)==T:
            drums[ev[ei][1]].strike(ev[ei][2]); ei+=1
    return out


class Kit:
    """A kit of ModalDrum instances plus the coupling between them.
    Sympathetic snare buzz: every other drum's sound drives each snare's head modes (below 1.5 kHz) as a driven
    resonator bank (air coupling). The snare heads ring along quietly, and when that motion lifts the head past the
    wires' seating level the wires rattle: the buzz follows the head motion above a threshold, with the snare's own
    wire-noise spectrum. One kit parameter: snare_buzz (0 = off)."""
    def __init__(self, drums, snare_buzz=0.5):
        self.drums=drums; self.snare_buzz=snare_buzz
        self.snares=[k for k,d in drums.items() if 'SNRE' in d.m.get('drum',getattr(d,'path',''))]
        self.sym={}
        for k in self.snares: self.sym[k]=self._sym_init(drums[k])
    def set(self,name,val):
        if name=='snare_buzz': self.snare_buzz=float(np.clip(val,0,1))
    def _sym_init(self,d):
        L=d.m['articulations']['head']['layers'] if 'articulations' in d.m else d.m['layers']
        l=L[0]; F=np.array(l['freq']); D=np.array(l['decay']); A=np.array(l['amp']); P=np.array(l['phase'])
        sel=(A>0)&(F<1500); F,D,A,P=F[sel],D[sel],A[sel],P[sel]
        t=np.arange(int(0.02*SR))/SR; own=np.real((A*np.exp(1j*P))@np.exp((-D[:,None]+2j*np.pi*F[:,None])*t[None,:]))
        href=np.max(np.abs(own))                                   # snare's own full-hit head motion (low modes)
        w=A/np.sqrt(np.sum(A**2))                                  # participation of each mode
        nb=np.array([b['peak_rms'] for b in l['noise']])           # the snare's own wire-noise spectrum (hardest hit)
        sos=[butter(2,[lo,min(hi,20000)],'band',fs=SR,output='sos') for lo,hi in d.bands]
        _r=np.random.default_rng(0).standard_normal(1<<16); bn=np.array([1/np.sqrt(np.mean(sosfilt(s_,_r)[2000:]**2)) for s_ in sos])
        return dict(F=F,D=D,w=w,href=href,nb=nb,sos=sos,bn=bn,zi=[np.zeros((2,2)) for _ in sos],
                    y=np.zeros(len(F),complex),env=0.0,rng=np.random.default_rng(23))
    def render(self,n):
        outs={k:d.render(n) for k,d in self.drums.items()}
        mix=sum(outs.values())
        if self.snare_buzz<=0 or not self.snares: return mix
        for k in self.snares:
            S=self.sym[k]; d=self.drums[k]
            x=mix-outs[k]                                          # everything except this snare
            if not np.any(x): S['y']*=np.exp((-S['D']+2j*np.pi*S['F'])*n/SR); S['env']*=np.exp(-n/(0.03*SR)); continue
            c=self.snare_buzz*0.00017*S["w"]                          # air coupling into each head mode
            p=np.exp((-S['D']*d._dmul(S['F'])+2j*np.pi*S['F']*d.tune)/SR)
            Y=np.empty((len(p),n),complex)
            for j in range(len(p)):
                yj,zf=lfilter([c[j]],[1,-p[j]],x,zi=[p[j]*S['y'][j]]); Y[j]=yj; S['y'][j]=yj[-1]
            head=np.real(Y.sum(0)); mix=mix+head                    # snare heads ringing along
            # wires: rattle when the head motion exceeds their seating level (fast attack, ~30 ms release)
            a=np.abs(head)/S['href']; env=np.empty(n); e=S['env']; ka=np.exp(-1/(0.0005*SR)); kr=np.exp(-1/(0.03*SR))
            for i in range(n):
                e=ka*e+(1-ka)*a[i] if a[i]>e else kr*e+(1-kr)*a[i]; env[i]=e
            S['env']=e
            thr=0.004; drive=np.clip((env-thr)/(0.25-thr),0,1)**0.7
            if drive.max()<1e-6: continue
            for j,sos in enumerate(S['sos']):
                nz,S['zi'][j]=sosfilt(sos,S['rng'].standard_normal(n),zi=S['zi'][j])
                mix=mix+nz*S['bn'][j]*S['nb'][j]*drive
        return mix
    def sequence(self,events,dur):
        """events: (time_s, drum_key, vel[, art])"""
        N_=int(dur*SR); out=np.zeros(N_); cur=0
        ev=sorted(events,key=lambda e:e[0]); ei=0
        while cur<N_:
            nxt=int(ev[ei][0]*SR) if ei<len(ev) else N_
            if nxt>cur: out[cur:nxt]=self.render(nxt-cur); cur=nxt
            while ei<len(ev) and int(ev[ei][0]*SR)<=cur:
                e=ev[ei]; self.drums[e[1]].strike(e[2],art=e[3] if len(e)>3 else 'head'); ei+=1
        return out
