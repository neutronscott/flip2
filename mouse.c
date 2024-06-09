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
#include <dirent.h>
#include <libevdev/libevdev-uinput.h>

#define DEV_INPUT "/dev/input"

#define DEPRESS 1
#define RELEASE 0

#define RC_MUTE   0
#define RC_KEYB   1
#define RC_MOUSE -1

#define DEBUG_MODE      0x0001
#define DEBUG_TIME      0x0002
#define DEBUG_MSC       0x0004
#define DEBUG_EVT_IN    0x0010
#define DEBUG_EVT_KEYB  0x0020
#define DEBUG_EVT_MOUSE 0x0040
#define DEBUG_EVT_MUTE  0x0080

/* ew globals */
int mousemode = 0;
int mousespeed = 4;
int debug = 0;
struct libevdev *virtmouse_dev;
struct libevdev_uinput *virtmouse_uidev;

const char *wanted_devs[] = { "mtk-kpd", "matrix-keypad", 0 };

struct dev_st {
  int fd;
  const char *name;
  struct libevdev *evdev;
  struct libevdev_uinput *uidev;
  struct dev_st *next;
};

struct dev_st *devs_head = NULL;

int attach_mouse();
int detach_mouse();

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
  result->tv_sec  = x->tv_sec  - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

void set_mouse(int val) {
  if (debug & DEBUG_MODE) {
    if (val) {
      printf("Enter mouse mode\n");
    } else {
      printf("Exit mouse mode\n");
    }
  }
  mousemode = val;
  if (val) { // wiggle it so user knows it's on and can stop holding button
    libevdev_uinput_write_event(virtmouse_uidev, EV_REL, REL_X,  1);
    libevdev_uinput_write_event(virtmouse_uidev, EV_REL, REL_X, -1);
    libevdev_uinput_write_event(virtmouse_uidev, EV_SYN, SYN_REPORT, 0);
  }
}

int toggle(struct dev_st *dev, struct input_event *ev, int value) {
  static struct timeval start;
  static int mute_scan    = 1;
  static int mute_release = 0;
  struct timeval diff;

  // you get a MSC_SCAN before first key. that sucks. mute that too.
  if (value == DEPRESS) {
    start = ev->time;
    mute_scan = 0;
    return RC_MUTE;
  } else if (value == RELEASE) {
    mute_scan = 1;
    if (mute_release) {
      mute_release = 0;
      return RC_MUTE;
    }
    if (mousemode) {
      // long press not required to exit mousemode
      set_mouse(0);
      return RC_MUTE;
    } else {
      // we need to add back the key-down that we ate earlier
      libevdev_uinput_write_event(dev->uidev, EV_KEY, KEY_MENU, 1);
      libevdev_uinput_write_event(dev->uidev, EV_SYN, SYN_REPORT, 0);
      return RC_KEYB; // and also send the key-up
    }
  } else if (!mute_scan) { // scan codes
    // do math to see if it's long press
    timeval_subtract(&diff, &ev->time, &start);
    if (debug & DEBUG_TIME) printf("Held for %ld.%06lds\n", diff.tv_sec, diff.tv_usec);
    if (mousemode) return RC_MUTE;
    if (diff.tv_sec > 0 || diff.tv_usec > 225000) {
      set_mouse(1);
      mute_release = 1;
      return RC_MUTE;
    }
  }

  return RC_MUTE;
}

#define SCROLL(c, v) \
  do { \
    if (ev->value == RELEASE && last_key == ev->code) { \
      libevdev_uinput_write_event(virtmouse_uidev, EV_REL, REL_Y, delta); \
      delta = 0; \
      libevdev_uinput_write_event(virtmouse_uidev, EV_SYN, SYN_REPORT, 0); \
      ev->type = EV_REL; ev->code = c; ev->value = v; goto mouse; \
    } else { \
      goto mute; \
    } \
  } while(0)

int map_code(struct dev_st *dev, struct input_event *ev) {
  int rc = RC_KEYB;
  static int last_key = -1;
  static int last_msc = -1;
  static int delta = 0;
  static unsigned int slowdown = 0;

  if (ev->type == EV_KEY) {
    // keys get a down and up event

    // only toggle keys are checked regardless of mousemode
    if (ev->code == KEY_MENU) {
      rc = toggle(dev, ev, ev->value);
      goto end;
    }

    // pass-thru everything if we're not in mousemode
    if (!mousemode) goto pass;

    // all the mousemode nonsense
    switch (ev->code) {
    // NOTE: need to mute key presses that are used in scan section
    case KEY_POWER: set_mouse(0);   goto pass;
    case KEY_ENTER: ev->code = BTN_LEFT; goto mouse;
    case KEY_UP:    SCROLL(REL_WHEEL,   1);
    case KEY_DOWN:  SCROLL(REL_WHEEL,  -1);
    case KEY_LEFT:  SCROLL(REL_HWHEEL,  1);
    case KEY_RIGHT: SCROLL(REL_HWHEEL, -1);
/*  case KEY_VOLUMEUP:
      if (ev->value == 1) mousespeed++;
      goto mute;
    case KEY_VOLUMEDOWN:
      if (ev->value == 1) mousespeed--;
      goto mute;
 */
    default:
      goto pass;
    }
  } else if (ev->type == EV_MSC) {
    // scan codes are repeated as long as a key is held down
    if (ev->code != MSC_SCAN)
      goto pass;
    if (ev->value == 33) { // KEY_MENU
      rc = toggle(dev, ev, 3);
      goto end;
    }
    if (!mousemode)
      goto pass;
    switch (ev->value) {
      case 35: // KEY_UP
        ev->type = EV_REL; ev->code = REL_Y; ev->value = -mousespeed; delta += mousespeed; goto mouse;
      case 9: // KEY_DOWN
        ev->type = EV_REL; ev->code = REL_Y; ev->value =  mousespeed; delta -= mousespeed; goto mouse;
      case 19: // KEY_LEFT
        ev->type = EV_REL; ev->code = REL_X; ev->value = -mousespeed; delta += mousespeed; goto mouse;
      case 34: // KEY_RIGHT
        ev->type = EV_REL; ev->code = REL_X; ev->value =  mousespeed; delta -= mousespeed; goto mouse;

/*      case 43: case 26:
        ev->type = EV_KEY; ev->code = BTN_LEFT;  ev->value = 1; goto mouse;
      case 25:
        ev->type = EV_KEY; ev->code = BTN_RIGHT; ev->value = 1; goto mouse;
      case 0: case 1:
        if (slowdown++ % 5) return 0;
        else ev->type = EV_REL; ev->code = REL_WHEEL; ev->value =  1; goto mouse;
      case 18: case 16:
        if (slowdown++ % 5) return 0;
        else ev->type = EV_REL; ev->code = REL_WHEEL; ev->value = -1; goto mouse;
      case 10:
        if (slowdown++ % 5) return 0;
        else ev->type = EV_REL; ev->code = REL_HWHEEL; ev->value =  1; goto mouse;
      case 8:
        if (slowdown++ % 5) return 0;
        else ev->type = EV_REL; ev->code = REL_HWHEEL; ev->value = -1; goto mouse;
 */
    }
  }
  // exit labels
pass:  rc = RC_KEYB;  goto end;
mouse: rc = RC_MOUSE; goto end;
mute:  rc = RC_MUTE;  goto end;
end:
  if (ev->type == EV_KEY && ev->value == RELEASE) {
    last_key = ev->code;
    delta = 0;
  } else if (ev->type == EV_MSC && ev->code == MSC_SCAN) {
    last_msc = ev->value;
  }
  return rc;
}

static void print_event(const char *prefix, struct input_event *ev)
{
  if (ev->type == EV_SYN) return;

  if (ev->type == EV_MSC && !(debug & DEBUG_MSC))
    return;
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

/* overkill but I wanted to... */
int find_devices() {
  int fd;
  struct dirent* file;
  DIR* dir;
  struct libevdev *evdev;
  struct dev_st *dev;

  dir = opendir(DEV_INPUT);
  if (dir == NULL) {
    perror("opendir");
    return -1;
  }

  for (file = readdir(dir); file; file = readdir(dir)){
    char file_path[256];
    char *name;
    int found = 0;
    if (file->d_type != DT_CHR)
      continue;

    snprintf(file_path, sizeof(file_path), "%s/%s", DEV_INPUT, file->d_name);

    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
      perror("open");
      continue;
    }

    if (libevdev_new_from_fd(fd, &evdev) < 0) {
      close(fd);
      continue;
    }

    for (int i = 0; wanted_devs[i]; i++) {
      if (strcmp(libevdev_get_name(evdev), wanted_devs[i]) == 0) {
        dev = malloc(sizeof(struct dev_st));
        dev->fd = fd;
        dev->name = libevdev_get_name(evdev);
        dev->evdev = evdev;
        dev->next = NULL;

        ioctl(dev->fd, EVIOCGRAB, 1);
        libevdev_uinput_create_from_device(dev->evdev, LIBEVDEV_UINPUT_OPEN_MANAGED, &(dev->uidev));

        if (!devs_head) {
          devs_head = dev;
        } else {
          struct dev_st *d = devs_head;
          while (d->next)
            d = d->next;
          d->next = dev;
        }
        found = 1;
        break;
      }
    }
    if (!found) {
      libevdev_free(evdev);
      close(fd);
    }
  }

  closedir(dir);
  return (devs_head == NULL);
}

int attach_mouse() {
  virtmouse_dev = libevdev_new();
  libevdev_set_name(virtmouse_dev, "vMouse");
  libevdev_enable_event_code(virtmouse_dev, EV_REL, REL_X, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_REL, REL_Y, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_REL, REL_WHEEL, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_REL, REL_HWHEEL, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_KEY, BTN_LEFT, NULL);
  libevdev_enable_event_code(virtmouse_dev, EV_KEY, BTN_RIGHT, NULL);
  libevdev_uinput_create_from_device(virtmouse_dev,
    LIBEVDEV_UINPUT_OPEN_MANAGED, &virtmouse_uidev);

  return 0;
}

int detach_mouse() {
  libevdev_uinput_destroy(virtmouse_uidev);
  libevdev_free(virtmouse_dev);

  return 0;
}

int main(int argc, char **argv) {
  struct input_event event;
  int rc = 0;
  int maxfd = 0;
  fd_set fds, rfds;

  if (argc > 1) {
    debug = atoi(argv[1]);
    printf("debug: %08X\n", debug);
  }

  if (find_devices()) {
    fprintf(stderr, "Found no input devices!\n");
    return -1;
  }
  attach_mouse();
  
  FD_ZERO(&fds);
  for (struct dev_st *d = devs_head; d; d = d->next) {
    FD_SET(d->fd, &fds);
    if (d->fd >= maxfd)
      maxfd = d->fd + 1;
  }
  for (;;) {
    rfds = fds;
    if (select(maxfd, &rfds, NULL, NULL, NULL) < 0) {
      perror("select");
      return -1;
    }
    for (struct dev_st *d = devs_head; d; d = d->next) {
      char prefix[6];
      if (FD_ISSET(d->fd, &rfds) == 0) {
        continue;
      }
      read(d->fd, &event, sizeof(event));
      if (debug & DEBUG_EVT_IN) {
        snprintf(prefix, 5, "<%d<", d->fd);
        print_event(prefix, &event);
      }

      rc = map_code(d, &event);
      if (rc == RC_KEYB) {
        if (debug & DEBUG_EVT_KEYB) {
          snprintf(prefix, 5, ">%d>", d->fd);
          print_event(prefix, &event);
        }
        libevdev_uinput_write_event(d->uidev, event.type, event.code,
            event.value);
        libevdev_uinput_write_event(d->uidev, EV_SYN, SYN_REPORT, 0);
      } else if (rc == RC_MOUSE) {
        if (debug & DEBUG_EVT_MOUSE) {
          print_event(">M>", &event);
        }
        libevdev_uinput_write_event(virtmouse_uidev, event.type, event.code,
            event.value);
        libevdev_uinput_write_event(virtmouse_uidev, EV_SYN, SYN_REPORT, 0);
      } else if (rc == RC_MUTE) {
        if (debug & DEBUG_EVT_MUTE) {
          print_event("SSS", &event);
        }
      }
    }
  }
}
