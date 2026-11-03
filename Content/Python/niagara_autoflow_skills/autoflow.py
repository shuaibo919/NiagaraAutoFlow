import unreal
from toolset_registry.agent_skill import agent_skill

_INSTRUCTIONS = """
## What NiagaraAutoFlow adds
The official Niagara toolsets cover structure (topology, schemas, input values) and editing.
NiagaraAutoFlow adds custom HLSL module authoring and the data below the stack. Use official tools for stack editing, AutoFlow authoring for managed module assets, and AutoFlow analysis to understand cost and correctness.

- Compiled: RecompileSystem, GetCompiledScripts, GetScriptSource, SearchScriptSource -- translated HLSL (CPU/GPU), VM assembly, op counts, registers, data interfaces, attribute writes per script.
- Attributes: GetParticleAttributeSet, GetAttributeFlow -- per-particle layout, bytes per particle, which scripts write an attribute and which renderers consume it.
- Topology: GetSimulationStages, GetEventHandlers, GetEmitterProfiles -- parts the official topology does not walk, plus typed emitter/renderer settings (materials, blend mode, sort mode).
- Scalability: GetQualityLevels, GetEffectTypeInfo, GetEffectiveScalability -- values that actually apply at a quality level after effect type and overrides merge.
- Audit: GetAuditRules, AuditSystem, AuditAssets -- rule-based static review with severities; batch audits write a JSON report.
- Runtime: ListActiveInstances, GetInstanceStats, CaptureParticleData -- live instances, particle counts and memory, sampled attribute values (needs a ticking world: PIE or realtime viewport).

## Custom module authoring
- Authoring: CreateModuleScriptFromHlsl, GetModuleScriptDefinition, UpdateModuleScriptDefinition, ValidateModuleScript.
- Create a reusable single-Custom-HLSL module from typed input/output bindings and a multiline function body assigning every output. Module.* inputs are editable controls; Particles/Emitter/System/Engine/User inputs read the parameter map.
- Creation is unsaved and unvalidated. Add the asset to a compatible enabled stack using official AddModule, then ValidateModuleScript in that system. If the context is busy, wait for its existing compilation to finish and retry. Save only after successful validation. Validate CPU and GPU separately when both are required.
- Prefer a real ValidationSystem for updates; rollback-on-failure restores the previous definition and recompiles. A missing context means no compile validation, no automatic rollback, and no save. Check bCompileSucceeded, bRolledBack, bRollbackCompileSucceeded and bSaved.
- Validation waits for GPU shaders too and rejects unrelated systems. A timeout or manual edit during validation prevents automatic save. Timeouts while the compiler is busy require explicit recovery using PreviousDefinition after compilation finishes.
- This editor requires the EnginePatches bridge. It does not edit arbitrary hand-authored graphs or versioned scripts. Defaults use Niagara pin syntax, not JSON vectors. position is distinct from float3 for LWC.

## Addressing
- Emitters by EmitterName exactly as GetSystemSummary reports it.
- Scripts by (EmitterName, ScriptUsage, UsageIndex) exactly as GetCompiledScripts reports it; EmitterName None for SystemSpawnScript / SystemUpdateScript.
  Emitter spawn/update stacks are inlined into the system scripts.
- Renderer indices match the official FNiagaraExt_StackItemReference::RendererIndex.
- Components by the ComponentPath from ListActiveInstances.

## HLSL is transient
HLSL, VM assembly and op counts are empty after an asset loads from DDC. Call RecompileSystem once per system per editor session before GetScriptSource / SearchScriptSource or reading LastOpCount.
Large HLSL: locate with SearchScriptSource first, then page with GetScriptSource (Offset/MaxChars).

## Review / optimization loop
1. AuditSystem (or AuditAssets for a folder). Triage High, then Medium.
2. For each finding, confirm with the matching detail tool before changing anything:
   payload / attribute findings -> GetParticleAttributeSet + GetAttributeFlow (+ SearchScriptSource to confirm no script reads an attribute);
   scalability findings -> GetEffectiveScalability at the relevant quality levels;
   sim stage findings -> GetSimulationStages;
   overdraw / sorting findings -> GetEmitterProfiles.
3. Apply fixes with the official NiagaraToolset_System edit tools (SetEmitterData, SetRendererData, SetSystemData, SetStackInputData). Never create or edit an Effect Type without asking -- it is shared by many systems.
4. Check the official GetSystemCompileState / GetStackIssues, then AuditSystem again.
5. When behaviour matters, compare CaptureParticleData before and after on the same component.

## Judgement
- Findings are heuristics. WrittenButNotRendered attributes are often read by later modules (Age, Lifetime, NormalizedAge); verify in HLSL before removing writes.
- Prefer suggesting changes over applying them when a fix changes the look (spawn counts, sorting, sim target). Explain the expected visual impact.
- Platform sets are evaluated by effect quality mask only; device-profile and CVar overrides are not modelled.
"""


def _available_toolsets(names):
    return [getattr(unreal, name) for name in names if hasattr(unreal, name)]


@agent_skill
class NiagaraAutoFlowSkill(unreal.AgentSkill):
    """Covers authoring reusable Custom HLSL module assets and analysing Niagara systems below the module stack -- compiled HLSL and VM data, particle attribute sets, simulation stages, effective scalability, rule-based audits and live runtime stats -- and the audit-fix-verify loop with the official Niagara toolsets."""

    instructions = _INSTRUCTIONS
    toolsets = _available_toolsets([
        "NiagaraAutoFlowToolset_Authoring",
        "NiagaraAutoFlowToolset_Compiled",
        "NiagaraAutoFlowToolset_Attributes",
        "NiagaraAutoFlowToolset_Topology",
        "NiagaraAutoFlowToolset_Scalability",
        "NiagaraAutoFlowToolset_Audit",
        "NiagaraAutoFlowToolset_Runtime",
        "NiagaraToolset_System",
    ])
