# omtfNanoTables_cff.py
#
# NanoAOD-style FlatTable configurations for:
#   - OMTF track candidates (BXVector<l1t::RegionalMuonCand>, BX=0 only)
#   - Generator-level muons (reco::GenParticle, status==1, |pdgId|==13)
#
# Requires (compiled locally in L1Trigger/L1MuNano):
#   SimpleOMTFTrackCandidateFlatTableProducer
#     = BXVectorSimpleFlatTableProducer<l1t::RegionalMuonCand>
#
# Requires (from base PhysicsTools/NanoAOD):
#   SimpleGenParticleFlatTableProducer
#     = SimpleFlatTableProducer<reco::GenParticle>

import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import Var, ExtVar

# ---------------------------------------------------------------------------
# OMTF track table
# Input: BXVector<l1t::RegionalMuonCand> from simOmtfPhase2Digis:OMTF
#        (process label "L1" from the GEN+SIM+DIGI+L1 step)
# Only BX=0 is stored (minBX=maxBX=0).
# All hardware-level quantities available on l1t::RegionalMuonCand.
# ---------------------------------------------------------------------------
OMTFTrackTable = cms.EDProducer(
    "SimpleOMTFTrackCandidateFlatTableProducer",
    src               = cms.InputTag('simOmtfPhase2Digis', 'OMTF'),
    minBX             = cms.int32(0),
    maxBX             = cms.int32(0),
    name              = cms.string("omtf"),
    doc               = cms.string("OMTF track candidates at BX=0"),
    cut               = cms.string(""),          # no pre-selection
    extension         = cms.bool(False),
    alwaysWriteBXValue = cms.bool(False),        # BX is always 0, save space
    variables = cms.PSet(
        # Charge sign: 0 = positive, 1 = negative
        Q          = Var("hwSign()",            "int16", doc="charge sign (0=pos, 1=neg)"),
        # Hardware pT in units of 0.5 GeV/c (LSB), range 0..511
        hwPt       = Var("hwPt()",              "int16", doc="hardware pT [0.5 GeV/c LSB]"),
        # Unconstrained pT (displaced muon estimator)
        hwPtUnc    = Var("hwPtUnconstrained()", "int16", doc="hardware unconstrained pT"),
        # Displacement DXY
        hwDXY      = Var("hwDXY()",             "int16", doc="hardware DXY displacement"),
        # Hardware eta: OMTF range roughly |hwEta| < 88 (ieta units, LSB=0.010875)
        hwEta      = Var("hwEta()",             "int16", doc="hardware eta [LSB=0.010875]"),
        # Hardware phi in global phi units
        hwPhi      = Var("hwPhi()",             "int16", doc="hardware phi"),
        # Quality word (0..15)
        hwQual     = Var("hwQual()",            "int16", doc="hardware quality (0..15)"),
        # Muon index within the processor (0..2 for OMTF)
        muIdx      = Var("muIdx()",             "int16", doc="muon index in processor (0..2)"),
        # Processor ID: 0..5 for OMTF (6 sectors per +/- side)
        processor  = Var("processor()",         "int16", doc="processor ID (0..5 for OMTF)"),
    ),
)

# ---------------------------------------------------------------------------
# Generator-level muon table
# Input: vector<reco::GenParticle> from genParticles (process "L1")
# Selects stable muons (status==1, |pdgId|==13).
# ---------------------------------------------------------------------------
genMuonNanoTable = cms.EDProducer(
    "SimpleGenParticleFlatTableProducer",
    src       = cms.InputTag("genParticles"),
    name      = cms.string("GenMuon"),
    doc       = cms.string("Generator-level stable muons (|pdgId|==13, status==1)"),
    cut       = cms.string("abs(pdgId) == 13 && status == 1"),
    singleton = cms.bool(False),
    extension = cms.bool(False),
    variables = cms.PSet(
        pt     = Var("pt",      float, precision=8, doc="generator pT [GeV]"),
        eta    = Var("eta",     float, precision=8, doc="generator eta"),
        phi    = Var("phi",     float, precision=8, doc="generator phi [rad]"),
        pdgId  = Var("pdgId",  int,              doc="PDG id (+13=mu-, -13=mu+)"),
        charge = Var("charge",  int,              doc="electric charge"),
        vx     = Var("vx",     float, precision=8, doc="production vertex x [cm]"),
        vy     = Var("vy",     float, precision=8, doc="production vertex y [cm]"),
        vz     = Var("vz",     float, precision=8, doc="production vertex z [cm]"),
        dz     = Var("vz",     float, precision=8, doc="signed longitudinal displacement dz [cm] (relative to nominal IP)"),
        beta   = Var("p()/energy()", float, precision=8, doc="generator beta = |p|/E"),
        gamma  = Var("energy()/mass()", float, precision=8, doc="generator gamma = E/m"),
        lXY    = Var("sqrt(vertex().x()*vertex().x() + vertex().y()*vertex().y())",
                     float, precision=8, doc="transverse displacement lXY = sqrt(vx^2+vy^2) [cm]"),
        # NOTE: dXY is the exact helix perigee impact parameter, NOT the naive
        # straight-line projection (-vx*sin(phi)+vy*cos(phi)). The gun
        # (EMTFTools/ParticleGuns/FlatRandomPtGunProducer2::newVertexWithD0) places
        # the production vertex on the gyration circle at radius rg from a center
        # offset by (dxy - rg) from the origin, so recovering the true dxy requires
        # inverting that circle geometry rather than a straight-line formula. The
        # straight-line approximation is only accurate when rg >> lXY (high pT /
        # small displacement) and can be wrong by O(1) cm or more for low-pT,
        # large-displacement muons (e.g. C25-C28 mild-displaced samples).
        #   rg = -pT/(0.003*B*q)  [cm], B = 3.811 T (same convention as the gun)
        #   center = (vx - rg*sin(phi), vy + rg*cos(phi))
        #   dXY = rg + q*|center|
        dXY    = Var(
            "(-pt()/(0.003*3.811*charge()))"
            " + charge()*sqrt("
            "(vx()-(-pt()/(0.003*3.811*charge()))*sin(phi()))"
            "*(vx()-(-pt()/(0.003*3.811*charge()))*sin(phi()))"
            " + (vy()+(-pt()/(0.003*3.811*charge()))*cos(phi()))"
            "*(vy()+(-pt()/(0.003*3.811*charge()))*cos(phi()))"
            ")",
            float, precision=8,
            doc="signed transverse impact parameter dXY [cm] (exact helix perigee d0, curvature-corrected; see note above)"),
        status = Var("status",  int,              doc="generator status (1=stable)"),
    ),
)

# ---------------------------------------------------------------------------
# Generator-level muon propagator to 1st and 2nd muon stations.
# Produces ValueMap<float> keyed over the full genParticles collection:
#   etaSt1/phiSt1 — propagated coordinates at MB1 (barrel) or ME1 (endcap)
#   etaSt2/phiSt2 — propagated coordinates at MB2 (barrel) or ME2 (endcap)
# Non-muon / non-stable entries are filled with -9 (sentinel for failed/skipped).
# Requires SteppingHelixPropagator to be loaded in the process
# (process.load("TrackPropagation.SteppingHelixPropagator.SteppingHelixPropagator_cfi")).
# ---------------------------------------------------------------------------
genParticlePropagator = cms.EDProducer(
    "GenParticlePropagator",
    src = cms.InputTag("genParticles"),
    muProp1st = cms.PSet(
        useTrack                    = cms.string("none"),
        useState                    = cms.string("atVertex"),
        useSimpleGeometry           = cms.bool(True),
        useStation2                 = cms.bool(False),
        fallbackToME1               = cms.bool(False),
        cosmicPropagationHypothesis = cms.bool(False),
        useMB2InOverlap             = cms.bool(False),
        propagatorAlong    = cms.ESInputTag("", "SteppingHelixPropagatorAlong"),
        propagatorAny      = cms.ESInputTag("", "SteppingHelixPropagatorAny"),
        propagatorOpposite = cms.ESInputTag("", "SteppingHelixPropagatorOpposite"),
    ),
    muProp2nd = cms.PSet(
        useTrack                    = cms.string("none"),
        useState                    = cms.string("atVertex"),
        useSimpleGeometry           = cms.bool(False),
        useStation2                 = cms.bool(True),
        fallbackToME1               = cms.bool(False),
        cosmicPropagationHypothesis = cms.bool(False),
        useMB2InOverlap             = cms.bool(True),
        propagatorAlong    = cms.ESInputTag("", "SteppingHelixPropagatorAlong"),
        propagatorAny      = cms.ESInputTag("", "SteppingHelixPropagatorAny"),
        propagatorOpposite = cms.ESInputTag("", "SteppingHelixPropagatorOpposite"),
    ),
)

# ---------------------------------------------------------------------------
# Generator-level provenance labels for GenMuon entries.
# Produces ValueMap<int> keyed to genParticles:
#   parentPdgId      — immediate mother PDG id
#   isDecayInFlight  — 1 for pion/kaon DIF-like ancestry, else 0
# ---------------------------------------------------------------------------
genMuonProvenance = cms.EDProducer(
    "GenMuonProvenanceProducer",
    src = cms.InputTag("genParticles"),
    # Enables GenMuon_tpMuonIdx (reciprocal of OMTFAllInputTree's TpMuon_genMuonIdx). Matches the
    # TrackingParticle tag used by DataROOTDumper2AllInput.cc / CandidateSimMuonMatcher.
    trackingParticles = cms.InputTag("mix", "MergedTrackTruth"),
)

# Attach propagated coordinates to the gen-muon table as external variables.
# The ValueMap indices align with the full genParticles vector; NanoAOD applies
# the table cut (abs(pdgId)==13 && status==1) automatically.
genMuonNanoTable.externalVariables = cms.PSet(
    etaSt1 = ExtVar(cms.InputTag("genParticlePropagator", "etaSt1"),
                    "float", doc="eta at 1st muon station (MB1/ME1); -9 if outside acceptance", precision=8),
    phiSt1 = ExtVar(cms.InputTag("genParticlePropagator", "phiSt1"),
                    "float", doc="phi at 1st muon station (MB1/ME1) [rad]; -9 if outside acceptance", precision=8),
    etaSt2 = ExtVar(cms.InputTag("genParticlePropagator", "etaSt2"),
                    "float", doc="eta at 2nd muon station (MB2/ME2); -9 if outside acceptance", precision=8),
    phiSt2 = ExtVar(cms.InputTag("genParticlePropagator", "phiSt2"),
                    "float", doc="phi at 2nd muon station (MB2/ME2) [rad]; -9 if outside acceptance", precision=8),
    parentPdgId = ExtVar(cms.InputTag("genMuonProvenance", "parentPdgId"),
                         int, doc="immediate mother PDG id (0 if unavailable)"),
    isDecayInFlight = ExtVar(cms.InputTag("genMuonProvenance", "isDecayInFlight"),
                             int, doc="1 if first non-muon ancestor is pi/K (DIF-like), else 0"),
    provenanceStatus = ExtVar(cms.InputTag("genMuonProvenance", "provenanceStatus"),
                              int, doc="lXY-consistent provenance: 0=prompt (lXY<promptLxyCm), "
                                       "1=decayInFlight (displaced, pi/K ancestor), "
                                       "2=otherDisplaced (displaced, non-pi/K ancestor, e.g. heavy flavor/LLP)"),
    tpMuonIdx = ExtVar(cms.InputTag("genMuonProvenance", "tpMuonIdx"),
                       int, doc="reciprocal index into the selected TpMuon table in OMTFAllInputTree "
                                "(TpMuon_genMuonIdx is the inverse of this map); -1 if no matching "
                                "selected TrackingParticle (|pdgId|==13, pt>1.5, |eta|<1.6)"),
)


# ---------------------------------------------------------------------------
# GMT hybrid stub tables
# Input: l1t::MuonStubCollection from l1tStubsGmt (runs as part of Phase-2
#        SimL1Emulator when the phase2_trigger modifier is active, which it is
#        in the Phase2C17I13M9 era used for HECIN production).
#
# Two collections are produced by Phase2L1TGMTStubProducer:
#   :tps   — endcap stubs fed into the TPS (Track-based Pair Selector) algorithm
#   :kmtf  — barrel stubs fed into the KMTF (Kalman Muon Track Finder)
#
# l1t::MuonStub coordinate conventions:
#   coord1 — global phi in units of 30°/2048 (integer)
#   coord2 — phi bending angle (barrel only; integer)
#   eta1   — eta coordinate 1, LSB = 3.0/512 (integer)
#   eta2   — eta coordinate 2, LSB = 3.0/512 (integer)
#   offline_coord1/2, offline_eta1/2 — same in physical (rad / η) units (float)
#   type   — detector type: 0=TwinMux/DT, 1=RPC-Barrel, 2=CSC, 3=RPC-Endcap
# ---------------------------------------------------------------------------
_stubVars = cms.PSet(
    # ---- hardware integer quantities ----
    # coord1 is stored as int32 (not int16): the KMTF (barrel) producer packs an 18-bit signed
    # phi word (up to +-131071) which overflows int16 (+-32767) for sector>=1 (truncating the
    # high 2 bits and making the raw integer non-decodable to global phi). See ROOT_BRANCHES.md
    # section E. TPS (endcap) coord1 fits int16 cleanly but is widened too for a uniform schema.
    coord1      = Var("coord1()",      "int", doc="phi [30deg/2048 units]; int32 (KMTF 18-bit word overflows int16)"),
    coord2      = Var("coord2()",      "int16", doc="phi bending angle (barrel only)"),
    eta1        = Var("eta1()",        "int16", doc="eta coord 1 [3.0/512 LSB]"),
    eta2        = Var("eta2()",        "int16", doc="eta coord 2 [3.0/512 LSB]"),
    etaQuality  = Var("etaQuality()",  "int16", doc="eta measurement quality (-1=not available)"),
    quality     = Var("quality()",     "int16", doc="stub quality"),
    etaRegion   = Var("etaRegion()",   "int16", doc="eta region (wheel in barrel, ring in endcap)"),
    phiRegion   = Var("phiRegion()",   "int16", doc="phi region (sector in barrel, chamber in endcap)"),
    depthRegion = Var("depthRegion()", "int16", doc="station number"),
    tfLayer     = Var("tfLayer()",     "int16", doc="track finder layer index"),
    bxNum       = Var("bxNum()",       "int16", doc="bunch crossing number"),
    stubType    = Var("type()",        "int16", doc="detector type: 0=TwinMux/DT, 1=RPC-B, 2=CSC, 3=RPC-E"),
    isBarrel    = Var("isBarrel()",    bool,    doc="true if barrel stub (type==1)"),
    isEndcap    = Var("isEndcap()",    bool,    doc="true if endcap stub (type==0)"),
    id          = Var("id()",           "int16", doc="stub id within chamber"),
    addr        = Var("address()",      int,    doc="packed address word"),
    kmtf_addr   = Var("kmtf_address()", int,    doc="KMTF-specific packed address word"),
    # ---- offline (physical) quantities ----
    offlineCoord1 = Var("offline_coord1()", float, precision=10, doc="phi 1 [rad]"),
    offlineCoord2 = Var("offline_coord2()", float, precision=10, doc="phi bending [rad] (barrel)"),
    offlineEta1   = Var("offline_eta1()",   float, precision=10, doc="eta 1"),
    offlineEta2   = Var("offline_eta2()",   float, precision=10, doc="eta 2"),
)

# Endcap stubs (TPS algorithm input) — covers |eta| > ~0.8
MuonStubTpsTable = cms.EDProducer(
    "SimpleMuonStubFlatTableProducer",
    src       = cms.InputTag("l1tStubsGmt", "tps"),
    name      = cms.string("MuonStubTps"),
    doc       = cms.string("GMT hybrid stubs (TPS/endcap, l1tStubsGmt:tps)"),
    cut       = cms.string(""),
    singleton = cms.bool(False),
    extension = cms.bool(False),
    variables = _stubVars,
)

# Barrel stubs (KMTF algorithm input) — covers |eta| < ~1.2
MuonStubKmtfTable = cms.EDProducer(
    "SimpleMuonStubFlatTableProducer",
    src       = cms.InputTag("l1tStubsGmt", "kmtf"),
    name      = cms.string("MuonStubKmtf"),
    doc       = cms.string("GMT hybrid stubs (KMTF/barrel, l1tStubsGmt:kmtf)"),
    cut       = cms.string(""),
    singleton = cms.bool(False),
    extension = cms.bool(False),
    variables = _stubVars,
)

# ---------------------------------------------------------------------------
# Raw trigger primitive tables — DT, CSC, RPC
#
# These use custom EDProducers defined in
#   L1Trigger/L1MuNano/plugins/PrimitiveDigiToFlatTableProducers.cc
#
# Input tags mirror those consumed by simOmtfPhase2Digis (see
#   L1Trigger/L1TMuonOverlapPhase2/python/simOmtfPhase2Digis_cfi.py).
# ---------------------------------------------------------------------------

# -- Run-3 DT phi primitives (L1MuDTChambPhContainer) ----------------------
DTPhiDigiTable = cms.EDProducer(
    "DTPhiDigiFlatTableProducer",
    src  = cms.InputTag("simDtTriggerPrimitiveDigis"),
    name = cms.string("DTPhiDigi"),
    doc  = cms.string(
        "Run-3 DT phi trigger primitives (L1MuDTChambPhContainer). "
        "phi = radial angle, phiB = bending angle (displaced-muon discriminant)."
    ),
)

# -- Phase-2 DT phi primitives (L1Phase2MuDTPhContainer) -------------------
Ph2DTPhiDigiTable = cms.EDProducer(
    "Ph2DTPhiDigiFlatTableProducer",
    src  = cms.InputTag("dtTriggerPhase2PrimitiveDigis"),
    name = cms.string("Ph2DTPhiDigi"),
    doc  = cms.string(
        "Phase-2 DT phi trigger primitives (L1Phase2MuDTPhContainer). "
        "t0 and chi2 are key displaced-muon variables."
    ),
)

# -- Phase-2 DT theta primitives (L1Phase2MuDTThContainer) -----------------
Ph2DTThetaDigiTable = cms.EDProducer(
    "Ph2DTThetaDigiFlatTableProducer",
    src  = cms.InputTag("dtTriggerPhase2PrimitiveDigis"),
    name = cms.string("Ph2DTThDigi"),
    doc  = cms.string(
        "Phase-2 DT theta trigger primitives (L1Phase2MuDTThContainer). "
        "k = local theta slope (pointing displacement estimator)."
    ),
)

# -- CSC correlated LCT digis (CSCCorrelatedLCTDigiCollection) -------------
CSCLCTDigiTable = cms.EDProducer(
    "CSCLCTDigiFlatTableProducer",
    src  = cms.InputTag("simCscTriggerPrimitiveDigis", "MPCSORTED"),
    name = cms.string("CSCLctDigi"),
    doc  = cms.string(
        "CSC correlated LCT trigger primitives after MPC sorting. "
        "slope/bend encode local phi direction (CSC displaced proxy). "
        "DetId fields: endcap, station, ring, chamber."
    ),
)

# -- RPC digis (RPCDigiCollection) -----------------------------------------
RPCDigiTable = cms.EDProducer(
    "RPCDigiFlatTableProducer",
    src         = cms.InputTag("simMuonRPCDigis"),
    name        = cms.string("RPCDigi"),
    doc         = cms.string(
        "RPC strip digis. bx encodes timing (late BX -> displaced muon indicator). "
        "DetId fields: region, ring, station, sector, layer, subsector, roll."
    ),
    maxBxRange  = cms.int32(2),   # keep only |bx| <= 2 to limit table size at PU200
)

# -- Event-level pileup summary (PileupSummaryInfo from addPileupInfo) -------
PileupTable = cms.EDProducer(
    "PileupInfoFlatTableProducer",
    src  = cms.InputTag("addPileupInfo"),
    name = cms.string("Pileup"),
    doc  = cms.string(
        "Event-level pileup summary. nPU/nTrueInt at BX=0 plus neighboring BX occupancy."
    ),
)

# ---------------------------------------------------------------------------
# CMS Task: producers to run together before the NanoAOD output step.
# Attach to the process via process.schedule.associate(p2OmtfNanoTablesTask)
# or via a dedicated cms.Path.
# ---------------------------------------------------------------------------
p2OmtfNanoTablesTask = cms.Task(
    genParticlePropagator,
    genMuonProvenance,
    OMTFTrackTable,
    genMuonNanoTable,
    MuonStubTpsTable,
    MuonStubKmtfTable,
    DTPhiDigiTable,
    Ph2DTPhiDigiTable,
    Ph2DTThetaDigiTable,
    CSCLCTDigiTable,
    RPCDigiTable,
    PileupTable,
)
