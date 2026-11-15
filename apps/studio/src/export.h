#pragma once
// The PATCH tab's exports (export.cpp). `id` names the files and the C++ symbols ("hollow_kick"). Each returns the path it wrote, or false
// with the reason in err.
//   export_synth   apps/synthlab/src/synths/gen_<id>.cpp: a SynthLab synth that plays the patch with its modules called directly (no
//                  runner), its values the Modular synth's list; added to SynthLab's synth_list.cpp. A patch with Macros: its values
//                  are the Macros alone (the list's MACROS section), every other value fixed at the patch's (abstract parameters). Built at SynthLab's next build (and
//                  the studio's), it can then be measured and optimised as the Kick was.
//   export_engine  apps/studio/exports/<id>_graph.cpp: the patch as the Ritual Machine engine's graph (engine/core/graph.h), its values
//                  converted to the engine modules' units; what the engine has no module for yet is listed at the end, with its values
//                  and cables (the modules to write).
//   export_patch   apps/studio/exports/<id>.patch: the patch as text (framework/modular/patch.h).
#include <string>
#include "modular/patch.h"

namespace studio {

// The most values a synth has on the board (SynthLab's lab::kMaxParams there: lab.h, LAB_MAX_PARAMS's default; the studio lists up to
// 256). export_synth refuses a patch with more: it would not build for the board.
constexpr int kBoardMaxParams = 128;
int  synth_values(const mod::Patch &p);          // how many values (section titles included) an exported synth of `p` has: the
                                                // Modular synth's first synth_values(p) (with Macros: their section)
bool export_synth(const mod::Patch &p, const std::string &id, std::string &path, std::string &err);
bool export_engine(const mod::Patch &p, const std::string &id, std::string &path, std::string &err);
bool export_patch(const mod::Patch &p, const std::string &id, std::string &path, std::string &err);
//   export_engine_synth  apps/synthlab/src/synths/eng_<id>.cpp: a SynthLab engine synth (lab.h GraphSynth): the patch as an engine
//                  graph of the catalog's modules as engine types (framework/modular/engine_modules.h), loaded when the synth is
//                  picked; its values as export_synth's; added to synth_list.cpp.
bool export_engine_synth(const mod::Patch &p, const std::string &id, std::string &path, std::string &err);

// The texts themselves (the self-test checks them; `synth_name` is the patch's name).
std::string synth_source(const mod::Patch &p, const std::string &id);
std::string engine_source(const mod::Patch &p, const std::string &id);
std::string engine_synth_source(const mod::Patch &p, const std::string &id);

}  // namespace studio
