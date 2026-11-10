# Monster-XMG-Gpu-Power-Control
This repository allows you to control the GPU cTDP—within the maximum power limits permitted by the GPU's VBIOS—on OEM laptops built on the Tongfang GM5 chassis, such as the Monster Tulpar T5 v21.7 (GM5TG7Y) and XMG Neo 15 M21 (GM5TG7W), which feature RTX 3060, 3070, or 3080 GPUs.

Please note that you cannot exceed the factory-set maximum GPU TDP using this program alone; to do so, you would first need to flash a higher-TDP VBIOS (compatible with your specific GPU) using `nvflash`.

Regarding VBIOS:
For my own device, I am using the 150W VBIOS from the MECHREVO Taitan GM7TG7M. You may experiment with other VBIOS versions, provided that the Device ID and Subsystem ID are compatible. (Ensure the MUX switch is disabled during testing.)

Mechrevo 150W VBIOS:
https://www.techpowerup.com/vgabios/249133/249133
Clevo 150W VBIOS:
https://www.techpowerup.com/vgabios/256918/256918

Unfortunately, even if you flash a 150W VBIOS, you cannot actually reach the 150W threshold without bypassing the restrictions imposed by the OEM and the NVIDIA driver. This is precisely what this program enables you to do: it allows you to unlock the power limits set by the OEM and NVIDIA.
