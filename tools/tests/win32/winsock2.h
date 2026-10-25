/*
 * Stand-in for <winsock2.h> in the host tests that build new_dynarec.c with
 * WIN32 defined (see windows.h here). Under WIN32, new_dynarec.c includes
 * <winsock2.h> for the BSD integer names such as u_int, which the host's
 * <sys/types.h> already gives it.
 */
#ifndef DYNAREC_TEST_WINSOCK2_H
#define DYNAREC_TEST_WINSOCK2_H
#endif
