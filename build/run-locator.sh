../bin/device_locator $(cat credentials.txt | cut -d$'\n' -f1) $(cat credentials.txt | cut -d$'\n' -f2)
