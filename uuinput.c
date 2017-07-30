/*
 * uuinput — a user-space proxy for uinput
 *
 * Copyright © 2017 Simon McVittie
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#define FUSE_USE_VERSION 29

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cuse_lowlevel.h>

static void
uuinput_open (fuse_req_t req,
    struct fuse_file_info *fi)
{
  int fd;

  fprintf (stderr, "open\n");

  fd = open ("/dev/uinput", fi->flags);

  if (fd < 0)
    {
      int saved_errno = errno;

      fprintf (stderr, "-> error: %s\n", strerror (saved_errno));
      fuse_reply_err (req, saved_errno);
      return;
    }

  fprintf (stderr, "-> %d\n", fd);
  fi->fh = fd;
  fuse_reply_open (req, fi);
}

static void
uuinput_release (fuse_req_t req,
    struct fuse_file_info *fi)
{
  fprintf (stderr, "release %d\n", fi->fh);

  if (close (fi->fh) < 0)
    {
      int saved_errno = errno;

      fprintf (stderr, "-> error: %s\n", strerror (saved_errno));
      fuse_reply_err (req, saved_errno);
    }
  else
    {
      fprintf (stderr, "-> success\n");
    }
}

static void
uuinput_read (fuse_req_t req,
    size_t count,
    off_t offset,
    struct fuse_file_info *fi)
{
  ssize_t ret;
  char buffer[65536];

  fprintf (stderr, "read %d: %zu bytes at %lld\n", fi->fh, count, (long long) offset);

  /* TODO: realloc a global buffer instead? */
  if (count > sizeof (buffer))
    count = sizeof (buffer);

  ret = pread (fi->fh, buffer, count, offset);

  if (ret < 0)
    {
      int saved_errno = errno;

      fprintf (stderr, "-> error: %s\n", strerror (saved_errno));
      fuse_reply_err (req, saved_errno);
      return;
    }

  /* TODO: if short read, should we try reading more? */
  fprintf (stderr, "-> success, %zd bytes\n", ret);
  fuse_reply_buf (req, buffer, ret);
}

static void
uuinput_write (fuse_req_t req,
    const char *buffer,
    size_t count,
    off_t offset,
    struct fuse_file_info *fi)
{
  ssize_t ret;

  fprintf (stderr, "write %d: %zu bytes at %lld\n", fi->fh, count, (long long) offset);

  /* Ignore offset because /dev/uinput doesn't like pwrite(), only write() */
  ret = write (fi->fh, buffer, count);

  if (ret < 0)
    {
      int saved_errno = errno;

      fprintf (stderr, "-> error: %s\n", strerror (saved_errno));
      fuse_reply_err (req, saved_errno);
      return;
    }

  /* TODO: if short write, should we try writing more? */
  fprintf (stderr, "-> success, %zd bytes\n", ret);
  fuse_reply_write (req, ret);
}

static void
uuinput_ioctl (fuse_req_t req,
    int cmd,
    void *arg,
    struct fuse_file_info *fi,
    unsigned int flags,
    const void *in_buf,
    size_t in_count,
    size_t out_count)
{
  int ret;

  fprintf (stderr, "ioctl on %d: %x\n", fi->fh, cmd);

  switch (cmd)
    {
      case UI_SET_EVBIT:
      case UI_SET_KEYBIT:
      case UI_SET_ABSBIT:
      case UI_SET_RELBIT:
      case UI_SET_MSCBIT:
      case UI_SET_LEDBIT:
      case UI_SET_SNDBIT:
      case UI_SET_FFBIT:
      case UI_SET_SWBIT:
      case UI_SET_PROPBIT:
          {
            int real_arg = (intptr_t) arg;

            fprintf (stderr, "  argument %d\n", real_arg);

            ret = ioctl (fi->fh, cmd, real_arg);

            if (ret < 0)
              {
                int saved_errno = errno;

                fprintf (stderr, "-> error %s\n", strerror (saved_errno));
                fuse_reply_err (req, saved_errno);
              }
            else
              {
                fprintf (stderr, "-> %d\n", ret);
                fuse_reply_ioctl (req, ret, NULL, 0);
              }
          }
        break;

      case UI_DEV_CREATE:
      case UI_DEV_DESTROY:
          {
            ret = ioctl (fi->fh, cmd);

            if (ret < 0)
              {
                int saved_errno = errno;

                fprintf (stderr, "-> error %s\n", strerror (saved_errno));
                fuse_reply_err (req, saved_errno);
              }
            else
              {
                fprintf (stderr, "-> %d\n", ret);
                fuse_reply_ioctl (req, ret, NULL, 0);
              }
          }
        break;

      default:
          {
            fprintf (stderr, "-> unhandled\n");
            fuse_reply_err (req, ENOSYS);
          }
        break;
    }
}

static const struct cuse_lowlevel_ops uuinput_ops =
{
  .open = uuinput_open,
  .read = uuinput_read,
  .write = uuinput_write,
  .ioctl = uuinput_ioctl,
};

int
main (int argc,
    char **argv)
{
  struct fuse_args args = FUSE_ARGS_INIT (argc, argv);
  struct cuse_info ci;
  const char *dev_info_argv[] = { "DEVNAME=uuinput" };

  if (fuse_opt_parse (&args, NULL, NULL, NULL) < 0)
    {
      fprintf (stderr, "Failed to parse options\n");
      return 1;
    }

  memset (&ci, 0, sizeof (ci));

  ci.dev_major = 0;
  ci.dev_minor = 0;
  ci.dev_info_argc = 1;
  ci.dev_info_argv = dev_info_argv;
  ci.flags = CUSE_UNRESTRICTED_IOCTL;

  return cuse_lowlevel_main (args.argc, args.argv, &ci, &uuinput_ops, NULL);
}
