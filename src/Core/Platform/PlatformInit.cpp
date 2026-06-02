#include "PlatformInit.h"

#ifdef _WIN32
  #include "Windows.h"
  #pragma comment(lib, "Winmm.lib") // timeBeginPeriod
#endif

namespace Core::Platform
{
  void Init()
  {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
  }
}