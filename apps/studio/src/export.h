#pragma once
// The PATCH tab's exports (export.cpp). `id` names the files and the C++ symbols ("hollow_kick"). Each returns the path it wrote, or false
// with the reason in err.
//   export_synth   apps/synthlab/src/synths/gen_<id>.cpp: a SynthLab synth that plays the patch with its modules called directly (no
//                  runner), its values the Modular synth's list; added to SynthLab's synth_list.cpp. Built at SynthLab's next build (and
//                  the studio's), it can then be measured and optimised as the Kick was.
//   export_engine  apps/studio/exports/<id>_graph.cpp: the patch as the Ritual Machine engine's graph (engine/core/graph.h), its values
//                  converted to the engine modules' units; what the engine has no module for yet is listed at the end, with its values
//                  and cables (the modules to write).
//   export_patch   apps/studio/exports/<id>.patch: the patch as text (framework/modular/patch.h).
#include <string>
#include "modular/patch.h"

namespace studio {

bool export_synth(const mod::Patch &p, const std::string &id, std::string &path, std::string &err);
bool export_engine(const mod::Patch &p, const std::string &id, std::string &path, std::string &err);
bool export_patch(const mod::Patch &p, const std::string &id, std::string &path, std::string &err);

// The texts themselves (the self-test checks them; `synth_name` is the patch's name).
std::string synth_source(const mod::Patch &p, const std::string &id);
std::string engine_source(const mod::Patch &p, const std::string &id);

}  // namespace studio
