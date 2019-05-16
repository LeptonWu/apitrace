/* Android doesn't support RTLD_DEEPBIND, so we can't use that flag
 * in dlopen. Here is the hack: patch_symbolic is a function which
 * add DT_SYMBOLIC/DF_SYMBOLIC to shared library so it will have same
 * effects when modified libraries been loaded.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char ELF[] = { 0x7F, 'E', 'L', 'F' };

static const int PT_DYNAMIC = 0x2;
static const int DT_SYMBOLIC = 16;
static const int DT_FLAGS = 30;
static const int DF_SYMBOLIC = 0x2;

#define ERROR(ret) do { \
	if (ret) \
		fprintf(stderr, "%s:%d ERROR %d\n", __func__, __LINE__, ret); \
	return ret; \
} while(0)

static int read_at(int fd, loff_t off, unsigned char *buf, size_t len)
{
	int ret = lseek(fd, off, SEEK_SET);
	if (ret < 0)
		return -errno;
	ret = read(fd, buf, len);
	if (ret < 0)
		return -errno;
	if (ret != len)
		return -EINVAL;
	return 0;
}

#define READ_AT(fd, off, buf, len) do { \
	int ret = read_at(fd, off, buf, len); \
	if (ret < 0) \
		ERROR(ret); \
} while (0)

static unsigned long long read_long(unsigned char *buf, size_t size,
				    unsigned char bits, unsigned short off,
				    unsigned short cnt)
{
	size_t start = off + cnt * sizeof(int) * bits;
	if (start + sizeof(int) * bits > size)
		return -1;
	switch (bits) {
	case 1:
		return *(unsigned int *)(buf + start);
	case 2:
		return *(unsigned long long *)(buf + start);
	default:
		return -1;
	}
	return -1;
}

static int copy_file(const char *from, const char *to)
{
	int f = open(from, O_RDONLY);
	if (f < 0)
		ERROR(-1);
	int t = open(to, O_WRONLY|O_CREAT|O_EXCL, 0600);
	if (t < 0) {
		close(f);
		ERROR(-1);
	}
	size_t len = lseek(f, 0, SEEK_END);
	lseek(f, 0, SEEK_SET);
	ssize_t ret = sendfile(t, f, NULL, len);
	close(t);
	close(f);
	if (ret != len)
		ERROR(-1);
	return 0;
}

static int patch_file(const char *fname, loff_t off, unsigned char bits,
		      unsigned long long value, const char *target)
{
	int ret = copy_file(fname, target);
	if (ret < 0)
		ERROR(ret);
	int fd = open(target, O_RDWR);
	if (fd < 0)
		ERROR(-errno);
	ret = lseek(fd, off, SEEK_SET);
	if (ret < 0)
		ERROR(-errno);
	unsigned int v32 = value;
	unsigned long long v64 = value;
	switch (bits) {
	case 1:
		ret = write(fd, &v32, sizeof(v32));
		break;
	case 2:
		ret = write(fd, &v64, sizeof(v64));
		break;
	}
	close(fd);
	if (ret < 0)
		ERROR(ret);
	return 1;
}

static int check_lib(const char *fname, const char *target)
{
	int fd = open(fname, O_RDONLY);
	unsigned long long e_phoff;
	unsigned short e_phentsize;
	unsigned short e_phnum;

	if (fd < 0)
		return -errno;
	unsigned char header[64];
	READ_AT(fd, 0, header, sizeof(header));
	if (memcmp(header, ELF, 4))
		ERROR(-EINVAL);
	unsigned char bits = header[4];
	if (bits != 1 && bits != 2)
		ERROR(-EINVAL);
	e_phoff = read_long(header, sizeof(header), bits, 0x18, 1);
	unsigned char *ptr = (header + 0x18 + 12 * bits + 6);
	e_phentsize = *(unsigned short *)ptr;
	ptr += 2;
	e_phnum = *(unsigned short *)ptr;
	switch (bits) {
	case 1:
		if (e_phentsize != 0x20)
			ERROR(-EINVAL);
		break;
	case 2:
		if (e_phentsize != 0x38)
			ERROR(-EINVAL);
		break;
	}
	int has_symbolic = 0;
	loff_t flags_off = 0;
	unsigned long long flags = 0;
	loff_t null_off = 0;
	int i;
	for (i = 0; i < e_phnum; ++i) {
		READ_AT(fd, e_phoff + i * e_phentsize, header, e_phentsize);
		if (*(unsigned int *)(header) != PT_DYNAMIC)
			continue;
		//off_t dym_off = e_phoff + i * e_phentsize + 4 * bits;
		//off_t dym_size_off = e_phoff + i * e_phentsize + 16 * bits ;
		loff_t p_offset = read_long(header, sizeof(header), bits, 0, 1);
		size_t p_filesz = read_long(header, sizeof(header), bits, 0, 4);
		unsigned char *dynamic = (unsigned char *)malloc(p_filesz);
		if (!dynamic)
			ERROR(-ENOMEM);
		READ_AT(fd, p_offset, dynamic, p_filesz);
		unsigned char *p = dynamic;
		for (; p + 2 * 4 * bits <= dynamic + p_filesz; p += 2 * 4 *
		     bits) {
			unsigned long long dt = read_long(p, 8 * bits,
							  bits, 0, 0);
			unsigned long long dv = read_long(p, 8 * bits,
							  bits, 0, 1);
			if (dt == DT_SYMBOLIC) {
				has_symbolic = 1;
				break;
			}
			if (dt == DT_FLAGS) {
				flags_off = p_offset + p - dynamic + 4 * bits;
				flags = dv;
				if (flags & DF_SYMBOLIC) {
					has_symbolic = 1;
					break;
				}
			}
			if (dt == 0 && p < dynamic + p_filesz - 2 * 4 * bits) {
				null_off = p_offset + p - dynamic;
				break;
			}
		}
		break;
	}
	close(fd);
	if (has_symbolic) {
		return 0;
	}
	if (!has_symbolic && !flags_off && !null_off) {
		ERROR(-EINVAL);
	}
	if (flags_off) {
		flags = flags | DF_SYMBOLIC;
		return patch_file(fname, flags_off, bits, flags, target);
	} else {
		flags = DT_SYMBOLIC;
		return patch_file(fname, null_off, bits, flags, target);
	}
}

/* For given library orig, it will check if it's linked with --BSymbolic,
 * if it is, it will create a modified version under dir
 * Return value is either the original path or modified path. */
const char *patch_symbolic(const char *orig, const char *dir, char *buf,
			       size_t len)
{
	const char *l = strrchr(orig, '/');
	if (l == NULL)
		l = orig;
	else
		l++;
	int ret = snprintf(buf, len, "%s/%s", dir, l);
	if (ret >= len)
		return orig;
	if (!access(buf, R_OK))
		return buf;
	ret = check_lib(orig, buf);
	if (ret <= 0)
		return orig;
	else
		return buf;
}
