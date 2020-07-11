../bin/wd_bridge -f ../fs_mount -ouser=$(cat credentials.txt | cut -d$'\n' -f1),pass=$(cat credentials.txt | cut -d$'\n' -f2),host=$(cat devid.txt)
