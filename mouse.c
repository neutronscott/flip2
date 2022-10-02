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

#define DISTANCE 4
#define DEV_INPUT "/dev/input"

/* ew a global */
int mousemode = 0;

const char *wanted_devs[] = { "mtk-kpd", "matrix-keypad", 0 };

struct dev_st {
  int fd;
  const char *name;
  struct libevdev *evdev;
  struct libevdev_uinput *uidev;
  struct dev_st *next;
};

struct dev_st *devs_head = NULL;

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
  static unsigned int slowdown = 0;
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
#ifdef DEBUG
        printf("Held for %ld.%06lds\n", diff.tv_sec, diff.tv_usec);
#endif
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
    // these keys are muted but their scan codes make the mouse wheel go
    case KEY_7:
    case KEY_NUMERIC_STAR:
    case KEY_9:
    case KEY_NUMERIC_POUND:
      return 0;
    case KEY_VOLUMEUP:
      if (ev->value != 1) return 0;
      ev->type = EV_REL; ev->code = REL_WHEEL; ev->value =  1; return -2;
    case KEY_VOLUMEDOWN:
      if (ev->value != 1) return 0;
      ev->type = EV_REL; ev->code = REL_WHEEL; ev->value = -1; return -2;
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
      case 43: case 26:
        ev->type = EV_KEY; ev->code = BTN_LEFT;  ev->value = 1; return -2;
      case 25:
        ev->type = EV_KEY; ev->code = BTN_RIGHT; ev->value = 1; return -2;
      case 0: case 1:
        if (slowdown++ % 5) return 0;
        else ev->type = EV_REL; ev->code = REL_WHEEL; ev->value =  1; return -2;
      case 18: case 16:
        if (slowdown++ % 5) return 0;
        else ev->type = EV_REL; ev->code = REL_WHEEL; ev->value = -1; return -2;
        
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

int setup_devices(struct libevdev **virtmouse_dev,
    struct libevdev_uinput **virtmouse_uidev) {
  struct dev_st *d;

  for (d = devs_head; d; d = d->next) {
    ioctl(d->fd, EVIOCGRAB, 1);
    libevdev_uinput_create_from_device(d->evdev, LIBEVDEV_UINPUT_OPEN_MANAGED, &(d->uidev));
  }

  *virtmouse_dev = libevdev_new();
  libevdev_set_name(*virtmouse_dev, "vMouse");
  libevdev_enable_event_code(*virtmouse_dev, EV_REL, REL_X, NULL);
  libevdev_enable_event_code(*virtmouse_dev, EV_REL, REL_Y, NULL);
  libevdev_enable_event_code(*virtmouse_dev, EV_REL, REL_WHEEL, NULL);
  libevdev_enable_event_code(*virtmouse_dev, EV_KEY, BTN_LEFT, NULL);
  libevdev_enable_event_code(*virtmouse_dev, EV_KEY, BTN_RIGHT, NULL);
  libevdev_uinput_create_from_device(*virtmouse_dev,
    LIBEVDEV_UINPUT_OPEN_MANAGED, virtmouse_uidev);

  usleep(100000);

  return 0;
}

int main(int argc, char **argv) {
  struct input_event event;
  struct libevdev *virtmouse_dev;
  struct libevdev_uinput *virtmouse_uidev;
  int mute_event = 0;
  int maxfd = 0;
  fd_set fds, rfds;

  if (find_devices()) {
    fprintf(stderr, "Found no input devices!\n");
    return -1;
  }

  if (setup_devices(&virtmouse_dev, &virtmouse_uidev)) {
    return -1;
  }
  
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
#ifdef DEBUG
      char prefix[6];
#endif
      if (FD_ISSET(d->fd, &rfds) == 0) {
        continue;
      }
      read(d->fd, &event, sizeof(event));
#ifdef DEBUG
      snprintf(prefix, 5, "<%d<", d->fd);
      print_event(prefix, &event);
#endif
      // pass-thru if it's remapped
      mute_event = map_code(&event);
      if (mute_event > 0) {
#ifdef DEBUG
        snprintf(prefix, 5, ">%d>", d->fd);
        print_event(prefix, &event);
#endif
        libevdev_uinput_write_event(d->uidev, event.type, event.code,
            event.value);
        libevdev_uinput_write_event(d->uidev, EV_SYN, SYN_REPORT, 0);
      } else if (mute_event < 0) {
#ifdef DEBUG
        print_event(">M>", &event);
#endif
        libevdev_uinput_write_event(virtmouse_uidev, event.type, event.code,
            event.value);
        libevdev_uinput_write_event(virtmouse_uidev, EV_SYN, SYN_REPORT, 0);
      }
    }
  }
}
