/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This file is part of the Hobbit monitor library, but was taken from the    */
/* "mutt" source archive and adapted for use in Hobbit. According to the      */
/* copyright notice in the "mutt" version, this file is public domain.        */
/*----------------------------------------------------------------------------*/

/*
 SHA-1 in C

 By Steve Reid <steve@edmweb.com>, with small changes to make it
 fit into mutt by Thomas Roessler <roessler@does-not-exist.org>.

*/

#ifndef _SHA1_H
# define _SHA1_H

typedef unsigned int uint32_t;

typedef struct {
  uint32_t state[5];
  uint32_t count[2];
  unsigned char buffer[64];
} mySHA1_CTX;

void mySHA1Init(mySHA1_CTX* context);
void mySHA1Update(mySHA1_CTX* context, const unsigned char* data, uint32_t len);
void mySHA1Final(unsigned char digest[20], mySHA1_CTX* context);

# define SHA_DIGEST_LENGTH 20

#endif
