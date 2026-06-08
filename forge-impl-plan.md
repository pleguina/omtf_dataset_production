# `forge` Refactored MVP Plan

## 0. Main idea

`forge` should not assume that everything runs in the same place.

The correct model is:

```text
CMSSW / lxplus / Condor = production backend
EOS = canonical artifact storage
GPU server = training backend and fast local cache
forge = control plane connecting the steps
manifests = contracts between environments
```

So `forge` is not just a Condor wrapper. It is an artifact-driven workflow tool.

The workflow should be:

```text
CMSSW campaign
  ↓
ROOT files on EOS
  ↓
root_manifest.json
  ↓
ROOT validation report
  ↓
PyG .pt graph shards
  ↓
graph_manifest.json
  ↓
graph validation report
  ↓
training_manifest.json
  ↓
remote GPU training
  ↓
model.pt + metrics.json + run_manifest.json
  ↓
published model artifact on EOS
```

The important rule is:

```text
Every step produces an artifact.
Every artifact has a manifest.
Every manifest records provenance.
Every environment consumes manifests, not random paths.
```

---

# 1. Core design decision

Official data should live in EOS.

Remote machines may have copies, but those copies are caches.

```text
EOS:
  official ROOT files
  official .pt graph shards
  official manifests
  official reports
  published model runs

GPU server:
  local copy of .pt shards for speed
  training outputs before publishing
```

This avoids the dangerous situation where the real dataset only exists in some private server folder.

---

# 2. Clean execution boundaries

## 2.1 CMSSW production boundary

Runs on lxplus / CMSSW / Condor.

Input:

```text
CMSSW fragment
cmsDriver config
Condor submit file
campaign definition
```

Output:

```text
ROOT files on EOS
root_manifest.json
collect_report.json
root_validation.json
root_validation.md
```

Training should not depend on this environment.

---

## 2.2 ML dataset boundary

Can run on lxplus or another machine.

Input:

```text
root_manifest.json
ROOT files
ROOT data contract
```

Output:

```text
PyTorch Geometric .pt graph shards
graph_manifest.json
graph_validation.json
graph_validation.md
```

This step should ideally need only:

```text
Python
uproot
awkward
torch
torch_geometric
XRootD access if reading EOS remotely
```

It should not require full CMSSW unless absolutely necessary.

---

## 2.3 Training boundary

Runs on a GPU server.

Input:

```text
graph_manifest.json
.pt graph shards
training config
model code
```

Output:

```text
model.pt
metrics.json
plots/
run_manifest.json
```

Training should not require:

```text
CMSSW
lxplus
Condor
ROOT files
EOS as live training filesystem
```

EOS is used to publish the final run, not necessarily to train directly.

---

# 3. Canonical EOS layout

Each campaign should have one canonical directory.

Example:

```text
/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/
├── campaign.yaml
├── registry_entry.json
│
├── root/
│   ├── output_000000.root
│   ├── output_000001.root
│   └── ...
│
├── graphs/
│   └── schema_v1_graph/
│       ├── shard_000000.pt
│       ├── shard_000001.pt
│       └── ...
│
├── manifests/
│   ├── root_manifest.json
│   ├── graph_manifest.json
│   ├── training_manifest.json
│   └── published_runs.json
│
├── reports/
│   ├── collect_report.json
│   ├── root_validation.json
│   ├── root_validation.md
│   ├── graph_validation.json
│   └── graph_validation.md
│
└── runs/
    └── edgecompat_G3_v1/
        ├── model.pt
        ├── config.yaml
        ├── metrics.json
        ├── plots/
        └── run_manifest.json
```

A GPU server can mirror part of this:

```text
/data/omtf/campaigns/G3_singleDisplacedOverlap_v1/
├── graphs/
│   └── schema_v1_graph/
├── manifests/
│   ├── graph_manifest.json
│   └── training_manifest.json
└── runs/
    └── edgecompat_G3_v1/
```

But the GPU copy is only a cache unless it is published back to EOS.

---

# 4. Revised command model

## 4.1 Commands that usually run on lxplus

```bash
forge submit campaigns/G3.yaml
forge status G3_singleDisplacedOverlap_v1
forge collect G3_singleDisplacedOverlap_v1
forge validate-root G3_singleDisplacedOverlap_v1
```

These handle:

```text
CMSSW
Condor
EOS ROOT outputs
ROOT validation
```

---

## 4.2 Commands that may run on lxplus or another server

```bash
forge make-graphs G3_singleDisplacedOverlap_v1
forge validate-graphs G3_singleDisplacedOverlap_v1
forge export-training G3_singleDisplacedOverlap_v1
```

These handle:

```text
ROOT manifest
ROOT files
.pt graph shards
graph manifest
graph validation
training handoff
```

---

## 4.3 Commands that run on the GPU server

```bash
forge sync G3_singleDisplacedOverlap_v1 --to /data/omtf/campaigns/G3_singleDisplacedOverlap_v1
forge verify-cache G3_singleDisplacedOverlap_v1
forge train G3_singleDisplacedOverlap_v1 --config configs/train_edgecompat.yaml
forge evaluate G3_singleDisplacedOverlap_v1 --run runs/edgecompat_G3_v1
forge publish-run runs/edgecompat_G3_v1 --to eos
```

These handle:

```text
local .pt cache
training
model outputs
metrics
publishing back to EOS
```

---

# 5. Campaign YAML

The campaign YAML describes the whole artifact chain, but it does not force all steps to run on the same machine.

Example:

```yaml
campaign:
  name: G3_singleDisplacedOverlap_v1
  owner: pleguina
  description: "Single displaced muon overlap sample"
  version: 1

production:
  backend: condor_lxplus
  cmssw_release: CMSSW_14_X_Y
  fragment: fragments/G3_singleDisplacedOverlap.py
  config: configs/G3_cfg.py
  sub_file: condor/G3.sub
  events_per_job: 500
  njobs: 1000

storage:
  canonical_backend: eos
  campaign_dir: /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1
  root_dir: /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/root
  graph_dir: /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/graphs/schema_v1_graph
  manifest_dir: /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/manifests
  report_dir: /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/reports

data:
  root_tree: simOmtfPhase2Digis/OMTFAllInputTree
  root_contract: contracts/omtf_allinput_v1.yaml
  graph_schema: schema_v1_graph
  sample_id: G3
  sample_kind: signal_displaced_muon

graph_conversion:
  backend: portable_python
  script: scripts/make_graphs.py
  output_format: pyg_pt_shards
  shard_pattern: shard_{procid:06d}.pt

training:
  allowed_backends:
    - local_gpu
    - slurm
    - ssh_gpu_server
  default_cache_dir: /data/omtf/campaigns/G3_singleDisplacedOverlap_v1
```

---

# 6. Registry versus manifests

The registry should stay small.

It answers:

```text
What campaigns exist?
Where is the canonical campaign directory?
What is the high-level state?
Where are the manifests?
```

Example registry entry:

```json
{
  "G3_singleDisplacedOverlap_v1": {
    "campaign_id": "G3_singleDisplacedOverlap_v1",
    "owner": "pleguina",
    "created_at": "2026-05-28T14:35:00+00:00",
    "campaign_yaml": "campaigns/G3.yaml",
    "campaign_yaml_sha256": "abc123",
    "repo_git_commit": "def456",

    "canonical_storage": {
      "backend": "eos",
      "campaign_dir": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1"
    },

    "production": {
      "backend": "condor_lxplus",
      "sub_file": "condor/G3.sub",
      "sub_file_sha256": "123abc",
      "cluster_id": 8765432,
      "njobs_expected": 1000,
      "status": "submitted"
    },

    "artifacts": {
      "root_manifest": null,
      "graph_manifest": null,
      "training_manifest": null,
      "published_runs": []
    },

    "validation": {
      "root_status": "not_started",
      "graph_status": "not_started"
    }
  }
}
```

The detailed file lists, hashes, schema information, and provenance should live in manifests, not in the registry.

---

# 7. Root manifest

Produced by:

```bash
forge collect G3_singleDisplacedOverlap_v1
```

Purpose:

```text
Record the official ROOT dataset generated by CMSSW/Condor.
```

Example:

```json
{
  "manifest_type": "root_manifest",
  "manifest_version": 1,
  "campaign_id": "G3_singleDisplacedOverlap_v1",
  "created_at": "2026-05-28T15:10:00+00:00",

  "source": {
    "production_backend": "condor_lxplus",
    "cluster_id": 8765432,
    "sub_file_sha256": "123abc",
    "repo_git_commit": "def456"
  },

  "storage": {
    "backend": "eos",
    "root_dir": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/root"
  },

  "summary": {
    "njobs_expected": 1000,
    "root_files_found": 982,
    "missing_jobs": 18,
    "duplicate_jobs": 0,
    "small_files": 2
  },

  "files": [
    {
      "procid": 0,
      "logical_name": "output_000000.root",
      "uri": "root://eosuser.cern.ch//eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/root/output_000000.root",
      "local_path_hint": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/root/output_000000.root",
      "size_bytes": 184923012,
      "sha256": "..."
    }
  ],

  "validation": {
    "status": "not_started",
    "report_json": null,
    "report_md": null
  }
}
```

The manifest should include both:

```text
/eos/... local CERN path
root://... XRootD URI
```

because lxplus and remote servers may access EOS differently.

---

# 8. Graph manifest

Produced by:

```bash
forge make-graphs G3_singleDisplacedOverlap_v1
forge validate-graphs G3_singleDisplacedOverlap_v1
```

Purpose:

```text
Record the official ML-ready .pt graph dataset.
```

Example:

```json
{
  "manifest_type": "graph_manifest",
  "manifest_version": 1,
  "campaign_id": "G3_singleDisplacedOverlap_v1",
  "artifact_type": "pyg_graph_shards",
  "schema": "schema_v1_graph",
  "created_at": "2026-05-28T17:00:00+00:00",

  "source": {
    "root_manifest_sha256": "root_manifest_hash_here",
    "root_contract": "omtf_allinput_v1",
    "conversion_script": "scripts/make_graphs.py",
    "conversion_git_commit": "abc123",
    "conversion_command": "forge make-graphs G3_singleDisplacedOverlap_v1"
  },

  "storage": {
    "canonical_backend": "eos",
    "graph_dir": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/graphs/schema_v1_graph"
  },

  "summary": {
    "num_shards": 982,
    "num_graphs": 176760,
    "num_empty_graphs": 0,
    "graph_schema_version": "schema_v1_graph"
  },

  "files": [
    {
      "procid": 0,
      "logical_name": "shard_000000.pt",
      "uri": "root://eosuser.cern.ch//eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/graphs/schema_v1_graph/shard_000000.pt",
      "local_path_hint": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/graphs/schema_v1_graph/shard_000000.pt",
      "size_bytes": 50230122,
      "sha256": "..."
    }
  ],

  "validation": {
    "status": "PASS",
    "report_json": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/reports/graph_validation.json",
    "report_md": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/reports/graph_validation.md"
  }
}
```

This is the object consumed by training.

Training should not care about:

```text
Condor cluster ID
CMSSW fragment
cmsDriver command
ROOT branch details
```

Training should care about:

```text
graph_manifest.json
.pt shards
training config
model code
```

---

# 9. Training manifest

Produced by:

```bash
forge export-training G3_singleDisplacedOverlap_v1
```

Purpose:

```text
Create a portable handoff package for the GPU server.
```

Example:

```json
{
  "manifest_type": "training_manifest",
  "manifest_version": 1,
  "campaign_id": "G3_singleDisplacedOverlap_v1",
  "created_at": "2026-05-28T18:00:00+00:00",

  "dataset": {
    "graph_manifest": "graph_manifest.json",
    "graph_manifest_sha256": "graph_manifest_hash_here",
    "schema": "schema_v1_graph",
    "num_shards": 982,
    "num_graphs": 176760
  },

  "canonical_storage": {
    "backend": "eos",
    "graph_dir": "/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/graphs/schema_v1_graph"
  },

  "recommended_cache": {
    "path": "/data/omtf/campaigns/G3_singleDisplacedOverlap_v1",
    "layout": "mirror"
  },

  "sync": {
    "method": "xrdcp_or_rsync",
    "verify_sha256": true
  },

  "training_requirements": {
    "requires_cmssw": false,
    "requires_torch": true,
    "requires_torch_geometric": true,
    "requires_uproot": false
  }
}
```

Then on the GPU server:

```bash
forge sync --manifest training_manifest.json --to /data/omtf/campaigns/G3_singleDisplacedOverlap_v1
forge verify-cache G3_singleDisplacedOverlap_v1
forge train G3_singleDisplacedOverlap_v1 --config configs/train_edgecompat.yaml
```

---

# 10. Run manifest

Produced by:

```bash
forge train G3_singleDisplacedOverlap_v1 --config configs/train_edgecompat.yaml
```

Purpose:

```text
Record exactly which dataset, code, config, and environment produced a model.
```

Example:

```json
{
  "manifest_type": "run_manifest",
  "manifest_version": 1,
  "run_id": "edgecompat_G3_v1",
  "campaign_id": "G3_singleDisplacedOverlap_v1",
  "created_at": "2026-05-28T22:00:00+00:00",

  "environment": {
    "host": "gpu-server-01",
    "backend": "local_gpu",
    "python_version": "3.11",
    "torch_version": "2.x",
    "cuda_available": true
  },

  "dataset": {
    "graph_manifest_sha256": "graph_manifest_hash_here",
    "local_cache": "/data/omtf/campaigns/G3_singleDisplacedOverlap_v1/graphs/schema_v1_graph",
    "cache_verified": true
  },

  "model": {
    "name": "EdgeCompatNet",
    "model_git_commit": "abc123",
    "config": "configs/train_edgecompat.yaml"
  },

  "training": {
    "epochs": 50,
    "batch_size": 256,
    "seed": 12345,
    "optimizer": "AdamW",
    "learning_rate": 0.001
  },

  "outputs": {
    "model_pt": "model.pt",
    "metrics_json": "metrics.json",
    "plots_dir": "plots"
  },

  "status": "completed"
}
```

Then:

```bash
forge publish-run runs/edgecompat_G3_v1 --to eos
```

publishes the model back to:

```text
/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/runs/edgecompat_G3_v1/
```

---

# 11. Refactored implementation phases

## Phase 0 — Define artifact and manifest model

Before building too much CLI code, define:

```text
Campaign
Artifact
Manifest
Canonical storage
Cache
Backend
Validation report
```

Minimum artifact types:

```text
root_files
graph_shards
training_run
model
```

Minimum manifest types:

```text
root_manifest.json
graph_manifest.json
training_manifest.json
run_manifest.json
```

---

## Phase 1 — Submit campaign with canonical storage awareness

First implementation can be:

```bash
python3 campaigns/submit.py condor/G3.sub \
  --campaign G3_singleDisplacedOverlap_v1 \
  --campaign-dir /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1 \
  --root-dir /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/root
```

It should:

```text
call condor_submit
parse cluster ID
count expected jobs
create campaign directories
record sub file hash
record git commit
record canonical storage path
write registry entry
```

---

## Phase 2 — Status command with artifact states

Command:

```bash
forge status G3_singleDisplacedOverlap_v1
```

It should show:

```text
Condor state
expected jobs
ROOT files found
root_manifest status
graph_manifest status
training_manifest status
published runs
validation state
```

---

## Phase 3 — Collect ROOT files and create root_manifest.json

Command:

```bash
forge collect G3_singleDisplacedOverlap_v1
```

It should:

```text
read expected jobs from registry
list ROOT files in EOS
extract procids
detect missing jobs
detect duplicate jobs
detect small files
record file sizes
record EOS URI and /eos path
optionally compute hashes
write root_manifest.json
write collect_report.json
update registry
```

---

## Phase 4 — Validate ROOT files

Command:

```bash
forge validate-root G3_singleDisplacedOverlap_v1
```

It should consume:

```text
root_manifest.json
```

It should validate:

```text
file opens with uproot
simOmtfPhase2Digis/OMTFAllInputTree exists
tree has entries
required branches exist
branch lengths are compatible
continuous fields have no NaN or Inf
track_id is not trivially empty for signal samples
stub multiplicity is reasonable
basic distributions are not obviously broken
```

It should write:

```text
root_validation.json
root_validation.md
```

and update `root_manifest.json`.

---

## Phase 5 — Make graph shards and create graph_manifest.json

Command:

```bash
forge make-graphs G3_singleDisplacedOverlap_v1
```

It should consume:

```text
root_manifest.json
contracts/omtf_allinput_v1.yaml
```

It should produce:

```text
.pt graph shards
graph_manifest.json
```

It should support two modes.

Canonical EOS mode:

```bash
forge make-graphs G3_singleDisplacedOverlap_v1 \
  --out /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/graphs/schema_v1_graph
```

Remote mode:

```bash
forge make-graphs G3_singleDisplacedOverlap_v1 \
  --root-manifest root_manifest.json \
  --out /data/omtf/G3/graphs/schema_v1_graph
```

If remote mode is used, the graph dataset should later be published back to EOS.

Rule:

```text
A graph dataset is official only after it has graph_manifest.json and is published to canonical storage.
```

---

## Phase 6 — Validate graph shards

Command:

```bash
forge validate-graphs G3_singleDisplacedOverlap_v1
```

It should check:

```text
all shards load with torch.load
required fields exist
tensor shapes are correct
no NaN or Inf
edge_index has shape [2, num_edges]
edge_index values are within node bounds
edge_y length matches number of edges
node labels match number of nodes
empty graph fraction is acceptable
truth labels are not trivially missing
```

It should write:

```text
graph_validation.json
graph_validation.md
```

and update `graph_manifest.json`.

---

## Phase 7 — Export training handoff

Command:

```bash
forge export-training G3_singleDisplacedOverlap_v1
```

It should produce:

```text
training_manifest.json
sync.sh
```

The training manifest tells the GPU server:

```text
where the official graph dataset is
which graph_manifest hash is expected
how many shards exist
whether CMSSW is required
which Python packages are required
recommended cache layout
how to verify copied files
```

---

## Phase 8 — Sync and verify cache on GPU server

Command:

```bash
forge sync --manifest training_manifest.json \
  --to /data/omtf/campaigns/G3_singleDisplacedOverlap_v1
```

Then:

```bash
forge verify-cache G3_singleDisplacedOverlap_v1 \
  --cache /data/omtf/campaigns/G3_singleDisplacedOverlap_v1
```

Checks:

```text
all expected shards are present
file sizes match
hashes match
graph manifest hash matches training manifest
schema version matches
```

---

## Phase 9 — Train model

Command:

```bash
forge train G3_singleDisplacedOverlap_v1 \
  --manifest /data/omtf/campaigns/G3_singleDisplacedOverlap_v1/manifests/training_manifest.json \
  --config configs/train_edgecompat.yaml \
  --out runs/edgecompat_G3_v1
```

It should produce:

```text
model.pt
metrics.json
config.yaml
plots/
run_manifest.json
```

Training consumes graphs, not ROOT files.

---

## Phase 10 — Publish run back to EOS

Command:

```bash
forge publish-run runs/edgecompat_G3_v1 \
  --campaign G3_singleDisplacedOverlap_v1 \
  --to eos
```

It should publish:

```text
model.pt
metrics.json
plots/
config.yaml
run_manifest.json
```

To:

```text
/eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/runs/edgecompat_G3_v1/
```

It should also update:

```text
/eos/.../manifests/published_runs.json
```

---

# 12. Final implementation order

The new order should be:

```text
0. Define artifact and manifest model
1. submit.py with canonical storage awareness
2. status.py with artifact states
3. collect.py producing root_manifest.json
4. validate_root.py consuming/updating root_manifest.json
5. make_graphs.py producing graph_manifest.json
6. validate_graphs.py consuming/updating graph_manifest.json
7. export_training.py producing training_manifest.json
8. sync.py + verify_cache.py for GPU servers
9. train.py producing run_manifest.json
10. publish_run.py copying model artifacts back to EOS
11. Add Condor ClassAds
12. Add full campaign YAML
13. Add data contracts
14. Add backend abstraction
15. Add plugin system
```

The first useful version can stop at step 7.

That already gives:

```text
CMSSW production tracked
ROOT files validated
.pt graph dataset official
training handoff portable
```

---

# 13. First real patch

The first patch should include:

```text
campaigns/
├── registry.json
├── submit.py
├── utils.py
└── schemas/
    └── registry_entry.schema.json
```

## `utils.py` responsibilities

```text
load_registry()
save_registry()
sha256_file(path)
git_commit()
count_jobs_in_sub_file(path)
parse_condor_cluster_id(stdout)
ensure_campaign_dirs(campaign_dir)
now_utc()
```

## `submit.py` responsibilities

```text
parse arguments
validate campaign ID
validate or create campaign dir
create root/ graphs/ manifests/ reports/ runs/ directories
call condor_submit
parse cluster ID
count expected jobs
create registry entry
save registry
print next commands
```

Example command:

```bash
python3 campaigns/submit.py condor/G3.sub \
  --campaign G3_singleDisplacedOverlap_v1 \
  --campaign-dir /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1 \
  --root-dir /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/root
```

Expected output:

```text
Submitting campaign:
  G3_singleDisplacedOverlap_v1

Condor:
  Sub file:       condor/G3.sub
  Cluster ID:     8765432
  Expected jobs:  1000

Canonical storage:
  Backend:        eos
  Campaign dir:   /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1
  ROOT dir:       /eos/user/p/pleguina/omtf_datasets/campaigns/G3_singleDisplacedOverlap_v1/root

Created:
  root/
  graphs/
  manifests/
  reports/
  runs/

Recorded:
  campaigns/registry.json

Next:
  python3 campaigns/status.py G3_singleDisplacedOverlap_v1
  python3 campaigns/collect.py G3_singleDisplacedOverlap_v1
```

---

# 14. What this fixes

The old workflow problem was:

```text
I submitted something to Condor, but I have to remember the cluster ID, manually check EOS, manually validate ROOT, manually build graphs, and then somehow train somewhere else.
```

The new workflow becomes:

```text
I submitted campaign G3_singleDisplacedOverlap_v1.
forge knows the production backend, EOS location, ROOT manifest, graph manifest, validation reports, training handoff, and published model runs.
```

This is the important conceptual upgrade.

`forge` becomes agnostic because the handoff between environments is done through manifests, not through shared assumptions about where the code is running.

---

# 15. Final definition

`forge v0.1` should be:

```text
An artifact tracker and handoff tool for CMSSW-to-ML campaigns.
```

It should support:

```text
1. Submit CMSSW/Condor campaign from lxplus.
2. Register canonical EOS campaign directory.
3. Collect ROOT outputs into root_manifest.json.
4. Validate OMTFAllInputTree.
5. Convert ROOT files to PyG .pt graph shards.
6. Produce graph_manifest.json.
7. Validate .pt graph shards.
8. Export training_manifest.json for remote GPU training.
```

Later it should support:

```text
9. Sync graph shards to GPU server.
10. Verify local cache.
11. Train model.
12. Produce run_manifest.json.
13. Publish model and metrics back to EOS.
```

The central rule is:

```text
EOS stores official artifacts.
Remote servers store caches and training outputs.
Manifests connect everything.
```
