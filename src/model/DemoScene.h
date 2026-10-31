#pragma once

#include "model/Scene.h"

namespace ses
{

// Diego's demonstrative scene (4 points, rhythm 1/4, mix 70 %). Editable like
// any other scene. Point timing is one quarter note apart (1, 2, 3, 4): the
// spec did not state it, this is the agreed assumption.
Scene makeDemoScene();
Document makeDefaultDocument();

} // namespace ses
