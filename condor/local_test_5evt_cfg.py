from job_cfg import process
import FWCore.ParameterSet.Config as cms

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(5))
process.TFileService.fileName = cms.string('omtf_hits_local5.root')
if hasattr(process, 'NANOOMTFoutput'):
    process.NANOOMTFoutput.fileName = cms.untracked.string('omtf_nano_local5.root')
if hasattr(process, 'FEVTSIMoutput'):
    process.FEVTSIMoutput.fileName = cms.untracked.string('omtf_raw_local5.root')
