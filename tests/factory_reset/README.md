<!-- SPDX-License-Identifier: Apache-2.0 -->
# Factory reset host tests

`run.sh` verifies the RTC-retained request marker, NVS erase scope and restart
sequencing without touching device flash. Failure injection confirms that an
erase error retains the pending marker for a later boot instead of silently
continuing with partially reset data.
