# Niagara Script Authoring engine patch

Base: UE **5.8.4-release**, commit `f45729c0b6f097038e72b52d366a62baeca3275b`.

`UE5.8.4-NiagaraScriptAuthoring.diff` records every engine source change for AutoFlow module authoring. It adds three files under `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/`, including the fluid graph implementation in `NiagaraFluidScriptAuthoring.inl`. Original engine files are unchanged. The manifest records normalized UTF-8/LF SHA-256 hashes. Generated headers, DLLs and build output are excluded.

## Apply to another clean engine checkout

Run from the UE root (Git metadata is not required):

```powershell
$patch = 'E:\UnrealSourceCode\SourceCodeProject\S1Game\Plugins\NiagaraAutoFlow\EnginePatches\UE5.8.4-NiagaraScriptAuthoring.diff'
git apply --check $patch
git apply $patch
```

To check whether the patch is already installed, use `git apply --reverse --check $patch`. Do not apply it a second time. Reverse application removes the three bridge files, so first remove/disable the plugin authoring dependency if reverting.

Close the editor and build the project normally, or compile the affected modules:

```powershell
& .\Engine\Build\BatchFiles\Build.bat S1GameEditor Win64 Development '-Project=E:\UnrealSourceCode\SourceCodeProject\S1Game\S1Game.uproject' -Module=NiagaraEditor -Module=NiagaraAutoFlow -WaitMutex -NoHotReloadFromIDE
```

The bridge is editor-only. It has no dependency on AutoFlow or the MCP registry. AutoFlow requires the patched engine to compile. Reapply and rebuild after replacing the engine source.

## Tool workflow

1. `CreateModuleScriptFromHlsl(AssetName, AssetPath, Definition)` creates an unsaved module below `/Game/`. It never overwrites an existing package.
2. Official `AddModule` attaches it to a compatible enabled stack.
3. `ValidateModuleScript(Script, ValidationSystem, bSaveOnSuccess)` compiles that context, including GPU shaders, and optionally saves **the module only**. Save the system separately using normal editor tools.
4. `UpdateModuleScriptDefinition` optionally validates the update in an existing context. By default a completed failed compile restores the previous definition and recompiles. Inspect `bRolledBack` and `bRollbackCompileSucceeded` separately.
5. `GetModuleScriptDefinition` reads a managed definition. Graphs edited manually outside the bridge and versioned scripts are rejected; arbitrary graphs are never rebuilt automatically.

Creation and context-free updates do not prove compilation success. Timeouts leave the compiler running and do not save or replace a busy graph; the returned `PreviousDefinition` permits explicit recovery after compilation finishes. Changes during validation prevent automatic save/rollback. CPU success is not proof of GPU compatibility; validate each required context. The whole context system must compile successfully.

## Definition example

```json
{
  "Inputs": [
    {"Name": "InVelocity", "Type": "float3", "Variable": "Particles.Velocity"},
    {"Name": "Drag", "Type": "float", "Variable": "Module.Drag", "DefaultValue": "2"},
    {"Name": "DeltaTime", "Type": "float", "Variable": "Engine.DeltaTime"}
  ],
  "Outputs": [
    {"Name": "OutVelocity", "Type": "float3", "Variable": "Particles.Velocity"}
  ],
  "AllowedUsages": ["ParticleUpdateScript"],
  "Hlsl": "float scale = max(0.0, 1.0 - Drag * DeltaTime);\nOutVelocity = InVelocity * scale;",
  "Description": "Custom drag authored with AutoFlow."
}
```

Types: `float`, `float2`, `float3`, `float4`, `quat`, `int`, `bool`, `color`, `position`. `position` is distinct from `float3` for LWC; `quat` uses Niagara's quaternion type for mesh orientation. Defaults use Niagara pin syntax, for example `1,2,3` for float3, not JSON arrays. The runtime type parser validates defaults. Inputs read the parameter map; `Module.*` exposes a module control. Every output must be assigned in the HLSL function body and writes an attribute in the allowed usage's scope. Standard Spawn/Update usages in System, Emitter and Particle scopes are supported; one definition cannot mix writable scopes. This managed definition API does not edit Data Interfaces, includes, Scratch Pad or arbitrary node graphs.

The separate `NiagaraAutoFlow.NiagaraAutoFlowToolset_Fluids` toolset creates GPU Grid2D systems and their generated simulation-stage modules. Its NiagaraEditor bridge supports typed Data Interface inputs and execution pins for Grid2D / render-target writes. `UpdateFluidModuleHlsl` accepts only modules created by that bridge; explicitly recompile the owning system and save after success.

The working examples are in `/Game/Autoflow/L_AF_VFXShowcase`. `NS_AF_IcePillarRush` accepts the world-space Position parameter `User.TargetPosition`; its `DemoLoop` variant repeats in the showcase. Both fluid systems use 128×128 grids, blur diffusion, semi-Lagrangian advection and a two-pass separable Gaussian pressure approximation. RT channels are density, original signed depth, velocity X and velocity Y. Stencil input uses bit 64 and requires Custom Depth with Stencil enabled. The current projection plane is world XZ with its Y coordinate supplied by `User.PlaneOrigin`. Solver source is in `Tools/Autoflow/fluid_stages.py` and exported `FluidHLSL/*.hlsl`. Stencil sampling depends on the active view; the mesh example samples its configured mesh in local space.

The portal's moving streams use stock Point Attraction Force, Vortex Force, Curl Noise Force, Drag, Solve Forces and Velocity, and Scale Color with Float From Curve. The spawn helper distributes births around the outer annulus with depth variation and tangential velocity. `InfallDebris` adds smaller bright dust, and `AccretionFilaments` renders continuous inner spirals on a mesh plane. The ring and opaque core share the force-field center at local Z=350. The showcase's `AF_PortalLens` post-process volume bends scene color locally; `MI_AF_PortalGravitationalLens` exposes `PortalCenterWS`, `LensRadius`, `LensingStrength` and `ChromaticSplit`. Update its world center when moving the portal actor. A sparse additive star backdrop makes the lens visible against the dark stage.

Crystals retain a compact route/growth helper and three mesh variants; stock Initial Mesh Orientation converts their birth rotation. Their tapered growth envelope now leans along the route, with brighter pale-blue transmission and stronger endpoint shard spray. Vapor and `IceFragments` use staggered stock Spawn Burst Instantaneous modules, Curl Noise, Drag and Solve Forces and Velocity. Shards additionally use Gravity Force and the current Update Mesh Orientation module. Native curves handle opacity and vapor expansion. `GroundFrost` uses a Mesh Renderer with `/Engine/BasicShapes/Plane`, a procedural frost/fracture material, and 16 staggered route footprints that spread just before the crystals and dissolve afterwards. `User.GroundOffset` is the plane's vertical offset from the route (-27 cm in this stage). The old multi-role analytic update and custom quaternion construction were removed from these emitters to resolve the DemoLoop Vector VM compilation error. Ice controls include `User.HeightVariation`, `User.ScatterWidth` (cm), `User.TiltAngle` (radians), `User.SweepDuration` and the exact `User.FragmentCount` (144 by default).

The active direct-sampling exhibit now uses `NS_AF_SkeletalProjection`: a Skeletal Mesh NDI calls `GetSkinnedVertexDataWS` on 192 sampled vertices, carries source velocity into the fluid, and retains the original world Y depth. `User.SourceSkeletalMesh` is bound with `OverrideSystemUserVariableSkeletalMeshComponent` to a looping Manny Idle component. The stencil exhibit also renders Manny with custom stencil bit 64. Both sources use `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`; the original `NS_AF_MeshProjection` remains available as the earlier static-mesh example. The shared advection, blur and Gaussian-pressure solver is unchanged. Authoring entry points are `build_reference_details.py`, `upgrade_reference_particles.py` / `polish_reference_details.py`, and `build_skeletal_fluid.py` under `Tools/Autoflow`.

For recording, `/Game/Autoflow/L_AF_VFXShowcase` arranges portal, ice, mesh fluid and stencil fluid in one row. The scene has fixed manual exposure, soft key/fill/rim area lights, a high-roughness floor and four 16:9 CineCamera close-ups. `/Game/Autoflow/Cinematics/LS_AF_Showcase_Recording` is a 38-second, 30 fps camera-cut sequence: overview 0–5 s, portal 5–12 s, ice 12–19 s, mesh fluid 19–26 s, stencil fluid 26–33 s, overview 33–38 s. A recording/render has not been exported. Keep the level viewport visible with realtime enabled for editor preview; an open asset-editor window may suspend that viewport's Niagara ticking.

The ice renderer uses `MI_AF_IceCrystal_Raymarch`, parented to `M_AF_IceCrystal`. It combines procedural cellular surface fissures, fracture normals, Thin Translucent colored transmission, enabled Index of Refraction, and bounded per-particle ray marching with Beer–Lambert absorption and approximate single scattering. The proxy estimates internal thickness; it does not intersect the exact closed mesh or perform path tracing. Instance-space sampling keeps the pattern attached to rotating/scaling Niagara meshes. `MI_AF_IceCrystal_Raymarch_Lite` uses 8 samples; the main instance uses 16, clamped to 4–24. GPU frame time has not been benchmarked. The pre-change material is preserved at `/Game/Autoflow/Materials/Archive/M_AF_IceCrystal_BeforeRaymarch`.

| Ice material parameter | Main instance | Meaning |
| --- | --- | --- |
| `SurfaceCrackWidth`, `FractureFrequency` | `0.014`, `0.095` | Surface fissure width and instance-space frequency. |
| `SurfaceFrost`, `CrackNormalStrength` | `0.10`, `0.8` | Frost coverage and fracture relief. |
| `IceIOR`, `RefractionStrength` | `1.31`, `0.90` | Optical index and art-directed refraction blend. |
| `VolumeDensity`, `LayerAbsorption` | `0.75`, `0.18` | Internal optical density and overlap compensation. |
| `InternalFractures`, `SubsurfaceStrength` | `0.65`, `2.0` | Raymarched internal fissures and scattering brightness. |
| `RayMarchSteps`, `MaxThicknessCm` | `16`, `38` | Integration budget and maximum estimated path. |

`AbsorptionPerCm`, `InternalLightDirectionWS` and `InternalLightColor` control the transmission tint and approximate incident light. `DebugInterior` is an inspection mode and is saved at zero. Authoring source: `Tools/Autoflow/build_raymarched_ice.py`, `ice_material_hlsl.py` and `finish_raymarched_ice.py`.

Both fluid systems expose the following live simulation controls. `AddFluidModuleInputs` adds their typed User pins to the generated advection module; it leaves existing inputs intact and requires a context compile afterwards.

| User parameter | Default | Meaning |
| --- | --- | --- |
| `RadialCenterUV` | `(0.5, 0.48)` | Force center in normalized grid coordinates; use `Vector2f` for both the type and default-value struct. |
| `RadialRadius` | `400` | Force radius in cm. |
| `RadialStrength` | `-160` | Radial acceleration in cm/s²; negative attracts and positive expands. |
| `RadialSwirl` | `260` | Signed tangential acceleration in cm/s². |
| `RadialFalloff` | `1.5` | Influence exponent; larger values concentrate the force near the center. |

The `MI_AF_Prism_Mesh` and `MI_AF_Prism_Stencil` material instances now refract and split the original scene color using `PostProcessInput0`, the supported scene-color input for this post-process pass. `RefractionStrength`, `Dispersion`, `SceneColorBlend`, `PrismGlow` and `FluidOpacity` control the material independently of the velocity simulation. Original-depth reconstruction and occlusion remain active.

## Automated tests

Run with a real graphics backend (GPU shader validation requires it):

```powershell
& .\Engine\Binaries\Win64\UnrealEditor-Cmd.exe 'E:\UnrealSourceCode\SourceCodeProject\S1Game\S1Game.uproject' /Engine/Maps/Entry -unattended -nop4 -nosplash -nosound -d3d12 -RenderOffscreen '-ExecCmds=Automation RunTests NiagaraAutoFlow.Authoring' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=E:\UnrealSourceCode\UE5.8\Saved\AutoFlowAuthoringTests'
```

Tests cover tool schemas, argument validation, multiple outputs, updates, external-edit protection, save/reload, CPU/GPU compilation and failed-update restoration. Inspect `index.json` in the report directory: editor process exit status alone does not establish test success. No existing project assets are edited by these tests.

## Maintaining this patch

After changing a bridge file, regenerate the diff and manifest together:

```powershell
python .\EnginePatches\update_engine_patch.py --engine-root 'E:\UnrealSourceCode\UE5.8'
```

The recorder targets UE 5.8.4 and an explicit three-file list; it does not traverse the large engine source tree. It verifies reverse applicability against the installed bridge and clean application in a temporary directory, then compares reconstructed file hashes. Add any future engine files to its explicit list and preserve their original baseline before expanding this patch beyond file additions.
