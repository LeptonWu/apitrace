#ifndef _PATCH_SYMBOLIC_H_
#define _PATCH_SYMBOLIC_H_
#ifdef __cplusplus
extern "C" {
#endif
/* For given library orig, it checks if it's linked with --BSymbolic,
 * if it isn't, it creates a modified version under dir.
 * Return value is either the original path or modified path. */
const char *patch_symbolic(const char *orig, const char *dir, char *buf,
			   size_t len);
#ifdef __cplusplus
}  // extern "C"
#endif
#endif
