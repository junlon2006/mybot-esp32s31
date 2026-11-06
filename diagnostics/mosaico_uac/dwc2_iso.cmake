# SPDX-License-Identifier: Apache-2.0
# Compile a narrowly patched copy; the pinned managed input remains immutable.
idf_component_get_property(tusb_dir espressif__tinyusb COMPONENT_DIR)
set(dwc2_input "${tusb_dir}/src/portable/synopsys/dwc2/dcd_dwc2.c")
file(SHA256 "${dwc2_input}" dwc2_hash)
if(NOT dwc2_hash STREQUAL "b02378ee5b90f4eb00fee6289ab7531432e7e41b91e618c0319890b03b9fb25f")
    message(FATAL_ERROR "Review the Mosaico ISO IN patch against the changed TinyUSB input")
endif()
file(READ "${dwc2_input}" dwc2_source)

# S31 DIEPTXFn requires at least 16 32-bit words even for a four-byte feedback
# endpoint. Apply the minimum after half-empty doubling, before capacity checks.
set(fifo_old [=[    // Check if free space is available
    TU_ASSERT(_dcd_data.dfifo_top >= fifo_size + dwc2->grxfsiz);]=])
set(fifo_new [=[    // S31 dedicated IN TxFIFO depth has a minimum of 16 words.
    fifo_size = tu_max16(fifo_size, 16);
    // Check if free space is available
    TU_ASSERT(_dcd_data.dfifo_top >= fifo_size + dwc2->grxfsiz);]=])
string(REPLACE "${fifo_old}" "${fifo_new}" dwc2_source "${dwc2_source}")

# S31 DIEPTSIZn.MC is 01 for one periodic IN packet per microframe. Set it
# before endpoint enable, under the driver's existing lock, including retries.
set(schedule_old [=[  deptsiz.packet_count = num_packets;
  dep->tsiz = deptsiz.value;

  // control
  dwc2_depctl_t depctl = {.value = dep->ctl};]=])
set(schedule_new [=[  deptsiz.packet_count = num_packets;
  dwc2_depctl_t depctl = {.value = dep->ctl};
  if (dir == TUSB_DIR_IN && depctl.type == DEPCTL_EPTYPE_ISOCHRONOUS) {
    deptsiz.mc_pid = 1; // Mosaico UAC: one ISO IN packet per microframe
  }
  dep->tsiz = deptsiz.value;

  // control]=])
string(REPLACE "${schedule_old}" "${schedule_new}" dwc2_source "${dwc2_source}")

set(retry_old [=[        deptsiz.packet_count    = tu_div_ceil(xfer->total_len, xfer->max_size);
        epin->tsiz              = deptsiz.value;]=])
set(retry_new [=[        deptsiz.packet_count    = tu_max16(1, tu_div_ceil(xfer->total_len, xfer->max_size));
        deptsiz.mc_pid          = 1; // ISO IN, including a zero-length packet
        epin->tsiz              = deptsiz.value;]=])
string(REPLACE "${retry_old}" "${retry_new}" dwc2_source "${dwc2_source}")

set(dwc2_generated "${CMAKE_CURRENT_BINARY_DIR}/dcd_dwc2_mosaico.c")
file(CONFIGURE OUTPUT "${dwc2_generated}" CONTENT "${dwc2_source}" @ONLY)
get_target_property(tusb_sources ${tusb_lib} SOURCES)
list(FIND tusb_sources "${dwc2_input}" dwc2_index)
if(dwc2_index EQUAL -1)
    message(FATAL_ERROR "Cannot locate the pinned TinyUSB DWC2 compilation unit")
endif()
list(REMOVE_AT tusb_sources ${dwc2_index})
list(INSERT tusb_sources ${dwc2_index} "${dwc2_generated}")
set_property(TARGET ${tusb_lib} PROPERTY SOURCES "${tusb_sources}")
target_include_directories(${tusb_lib} PRIVATE "${tusb_dir}/src/portable/synopsys/dwc2")
