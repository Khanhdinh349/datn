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
  "CMakeFiles/_project_elf_src"
  "project_elf_src_esp32.c"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/_project_elf_src.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
