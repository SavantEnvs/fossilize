#!/bin/bash

# Change as needed
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json
#export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json

# For NV since they change disk cache location.
export __GL_SHADER_DISK_CACHE_APP_NAME=dummy

# Change as needed.
export PATH="$HOME/git/vkd3d-proton/build-native/demos:$PATH"

rm -rf ~/.cache/nvidia
rm -rf ~/.cache/mesa_shader_cache

function run_test() {
	rm -f /tmp/gears.*.foz
	echo "Press escape ..."
	FOSSILIZE=1 FOSSILIZE_DUMP_PATH=/tmp/gears gears 2>/dev/null >/dev/null
	# Fill in vkd3d-proton.cache. Fossilize skips that.
	echo "Press escape ..."
	gears 2>/dev/null >/dev/null

	# Clear the caches for different drivers
	rm -rf ~/.cache/nvidia
	rm -rf ~/.cache/mesa_shader_cache

	echo "Expecting to miss cache and fallback since cache is cleared ..."
	echo "Press escape ..."
	gears 2>&1 | grep IDENTIFIER

	rm -rf ~/.cache/nvidia
	rm -rf ~/.cache/mesa_shader_cache

	echo "Playing back .foz ..."
	fossilize-replay --num-threads 1 /tmp/gears.*.foz 2>/dev/null
	echo "Driver cache should be primed now ..."

	echo "Expecting to hit cache perfectly."
	echo "Press escape ..."
	gears 2>&1 | grep IDENTIFIER
}

echo "Testing descriptor buffer path."
export VKD3D_CONFIG=pipeline_library_log
run_test

echo "Testing descriptor heap path."
export VKD3D_CONFIG=pipeline_library_log,descriptor_heap
run_test

