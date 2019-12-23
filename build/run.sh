../bin/wd_bridge $(cat credentials.txt | cut -d$'\n' -f1) $(cat credentials.txt | cut -d$'\n' -f2) ../fs_mount/ $(cat devid.txt)
