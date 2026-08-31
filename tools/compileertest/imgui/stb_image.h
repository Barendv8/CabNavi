#pragma once
extern "C" {
unsigned char *stbi_load( const char *, int *, int *, int *, int );
unsigned char *stbi_load_from_memory( const unsigned char *, int, int *, int *, int *, int );
void stbi_image_free( void * );
}
extern "C" const char *stbi_failure_reason();
