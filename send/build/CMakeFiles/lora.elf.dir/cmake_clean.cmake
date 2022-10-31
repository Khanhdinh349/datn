file(REMOVE_RECURSE
  "bootloader/bootloader.bin"
  "bootloader/bootloader.elf"
  "bootloader/bootloader.map"
  "config/sdkconfig.cmake"
  "config/sdkconfig.h"
  "flash_project_args"
  "lora.bin"
  "lora.map"
  "project_elf_src_esp32.c"
  "CMakeFiles/lora.elf.dir/project_elf_src_esp32.c.obj"
  "CMakeFiles/lora.elf.dir/project_elf_src_esp32.c.obj.d"
  "lora.elf"
  "lora.elf.pdb"
  "project_elf_src_esp32.c"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/lora.elf.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
