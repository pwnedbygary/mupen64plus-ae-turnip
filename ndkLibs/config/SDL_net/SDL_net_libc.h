/*
  Not part of SDL_net: a local addition of this app's build (see ndkLibs/SDL_net/VENDOR.md).

  The build defines WITHOUT_SDL, so SDL_net is built without SDL, but its sources still call
  these six SDL functions. The build includes this header before each SDL_net source file
  (-include), and it maps them to the C library, as the 2021 build's edited SDL_net.h did.
*/
#ifndef SDL_NET_LIBC_H_
#define SDL_NET_LIBC_H_

#define SDL_malloc malloc
#define SDL_realloc realloc
#define SDL_free free
#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_vsnprintf vsnprintf

#endif /* SDL_NET_LIBC_H_ */
