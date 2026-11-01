/*
 * GlideMix
 */
#pragma once

#include <functional>
#include <string>

namespace au::mixhouse {
//! The transition names of a project's pairs, saved with the project (the
//! `glidemix_transitions` attribute), as MixMeister saves each track's TRSO.
//! `au3Project` is IAudacityProject::au3ProjectPtr().
//! `writer` returns what to save; it is asked each time the project is saved.
void setTransitionNamesWriter(void* au3Project, std::function<std::string()> writer);
//! What the project file held when it was opened ("" for a new project).
std::string loadedTransitionNames(void* au3Project);
}
