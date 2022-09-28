/*
 * for enabling virtual mouse anywhere on the Alcatel TCL FLIP 2
 * scott nicholas <scott@nicholas.one> 2022-09-07
 */

#include <fcntl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libevdev/libevdev-uinput.h>

#define DISTANCE 4

/* ew a global */
int mousemode = 0;

/* from gnu libc manual
 * https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html
 */

int
timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

// returns
// -2: changed to mouse
// 0: mute event
// 1: pass-thru event
// 2: changed event
int map_code(struct input_event *ev) {
  char cmd[64];
  int ret = -1;
  static struct timeval start;
  struct timeval diff;

  // start with just worrying about toggle key
  if (ev->type == EV_KEY) {
    switch (ev->code) {
    case KEY_CAMERA:
    case KEY_HELP:
      if (ev->value == 1) {
        start = ev->time;
      } else if (ev->value == 0) {
        timeval_subtract(&diff, &ev->time, &start);
        printf("Held for %ld.%06lds\n", diff.tv_sec, diff.tv_usec);
        mousemode = !mousemode;
      }
      return 0;
    }
  }
  if (!mousemode)
    return 1;

  if (ev->type == EV_KEY) {
    switch (ev->code) {
    case KEY_ENTER:
      ev->code = BTN_LEFT;
      return -2;
    // these keys are muted but their scan codes make the mouse go
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT:
      return 0;
    }
  } else if (ev->type == EV_MSC) {
    if (ev->code != MSC_SCAN)
      return 1;
    switch (ev->value) {
      case 35:
        ev->type = EV_REL; ev->code = REL_Y; ev->value = -DISTANCE; return -2;
      case 9:
        ev->type = EV_REL; ev->code = REL_Y; ev->value =  DISTANCE; return -2;
      case 19:
        ev->type = EV_REL; ev->code = REL_X; ev->value = -DISTANCE; return -2;
      case 34:
        ev->type = EV_REL; ev->code = REL_X; ev->value =  DISTANCE; return -2;
      case 43:
        ev->type = EV_KEY; ev->code = BTN_LEFT; ev->value = 1; return -2;
      case 42:
        break;
    }
  }
  return 1;
}

static void print_event(const char *prefix, struct input_event *ev)
{
  if (ev->type != EV_SYN)
    printf("%s [%s] Event: time %ld.%06ld, type %d (%s), code %d (%s), value %d\n",
      prefix,
      mousemode ? "GRAB" : "PASS",
      ev->input_event_sec,
      ev->input_event_usec,
      ev->type,
      libevdev_event_type_get_name(ev->type),
      ev->code,
      libevdev_event_code_get_name(ev->type, ev->code),
      ev->value);
}

int main(int argc, char **argv) {
  const char *virtkbd_path, *virtmouse_path;

  int uinput_fd;
  int mute_event = 0;
  int kbd_fd;

  struct input_event event;
  struct libevdev *kbd_dev;
  struct libevdev *virtkbd_dev, *virtmouse_dev;
  struct libevdev_uinput *virtkbd_uidev, *virtmouse_uidev;

  if (argc != 2) {
    fprintf(stderr, "usage: %s /dev/input/event#\n", argv[0]);
    return -1;
  }

  kbd_fd = open(argv[1], O_RDONLY);
  ioctl(kbd_fd, EVIOCGRAB, 1);
  libevdev_new_from_fd(kbd_fd, &kbd_dev);

  uinput_fd = open("/dev/uinput", O_RDWR);  

  libevdev_uinput_create_from_device(kbd_dev, uinput_fd, &virtkbd_uidev);

  virtmouse_dev = libevdev_new();
  libevdev_set_name(virtmouse_dev, "vMouse");
  libevdev_enable_event_code(virtmouse_dev, EV_REL, REL_X, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_REL, REL_Y, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_REL, REL_WHEEL, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_KEY, BTN_LEFT, NULL);
  libevdev_uinput_create_from_device(virtmouse_dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &virtmouse_uidev);
  virtkbd_path = libevdev_uinput_get_devnode(virtkbd_uidev);
  virtmouse_path = libevdev_uinput_get_devnode(virtmouse_uidev);
  printf("Virtual keyboard:%s mouse:%s\n", virtkbd_path, virtmouse_path);

  usleep(100000);
  
  for (;;) {
    read(kbd_fd, &event, sizeof(event));
    print_event("<<<", &event);
    // pass-thru if it's remapped
    mute_event = map_code(&event);
    if (mute_event > 0) {
      print_event(">>>", &event);
      libevdev_uinput_write_event(virtkbd_uidev, event.type, event.code, event.value);
      libevdev_uinput_write_event(virtkbd_uidev, EV_SYN, SYN_REPORT, 0);
    } else if (mute_event < 0) {
      print_event(">>>", &event);
      libevdev_uinput_write_event(virtmouse_uidev, event.type, event.code, event.value);
      libevdev_uinput_write_event(virtmouse_uidev, EV_SYN, SYN_REPORT, 0);
    }
  }
}
