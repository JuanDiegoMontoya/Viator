#include "Head.h"
#include "World.h"
#include "Game.h"
#include "Audio.h"

NullHead::NullHead()
{
  audio_ = std::make_unique<NullAudio>();
}

void NullHead::VariableUpdatePre(DeltaTime, World&) {}
void NullHead::VariableUpdatePost(DeltaTime, World&) {}

Audio* NullHead::GetAudio()
{
  return audio_.get();
}
