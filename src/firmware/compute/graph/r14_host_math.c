/* Use the Microsoft runtime for the unchanged host reference's libm calls.
 * No fallback, approximation or table patches. No deployment ARM dependency. */
#include "r14_host_math.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
typedef float (__cdecl *math_function)(float);
static HMODULE runtime;
static math_function exponential,logarithm,hyperbolic;
int r14_host_math_init(void){
 wchar_t path[MAX_PATH];UINT n;
 if(runtime)return 0;
 n=GetSystemDirectoryW(path,MAX_PATH);
 if(!n || n+14u>=MAX_PATH)return 1;
 memcpy(path+n,L"\\ucrtbase.dll",14u*sizeof(wchar_t));
 runtime=LoadLibraryW(path);if(!runtime)return 2;
 exponential=(math_function)GetProcAddress(runtime,"expf");
 logarithm=(math_function)GetProcAddress(runtime,"logf");
 hyperbolic=(math_function)GetProcAddress(runtime,"tanhf");
 if(!exponential || !logarithm || !hyperbolic){FreeLibrary(runtime);runtime=0;return 3;}
 return 0;
}
float r14_host_expf(float v){if(!exponential && r14_host_math_init())abort();return exponential(v);}
float r14_host_logf(float v){if(!logarithm && r14_host_math_init())abort();return logarithm(v);}
float r14_host_tanhf(float v){if(!hyperbolic && r14_host_math_init())abort();return hyperbolic(v);}
