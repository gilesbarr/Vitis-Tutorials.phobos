# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Intel Corporation

if is_windows
    build = false
    reason = 'not supported on Windows'
    subdir_done()
endif

# Copied from the kni library.  Note the funky trick in the meson.build 
# in drivers/net/kni to require the kni part is present
sources = files('rte_eth_xrta.c')
deps += 'kni'
