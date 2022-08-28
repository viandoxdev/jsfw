#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>

void hid_main() {
  DIR *d;
  struct dirent *dir;
  d =  opendir("/sys/class/hidraw");
  if(d) {
    while ((dir = readdir(d)) != NULL) {
      if(dir->d_type != DT_LNK) continue;
      printf("%s\n", dir->d_ino);
    }
    closedir(d);
  }
  exit(0);
}
