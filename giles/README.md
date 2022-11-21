
<table width="100%">
 <tr width="100%">
    <td align="center"><img src="https://raw.githubusercontent.com/Xilinx/Image-Collateral/main/xilinx-logo.png" width="30%"/><h1>DUNE Phobos - DPDK development with Xilinx FPGAs using XRT/Vitis</h1>
    </td>
 </tr>
</table>

## Experimentation version 0.1
This development has a goal of demonstrating high data bandwidth transfer of data from a Xilinx U50 device through a DPDK poll-mode driver into the DUNE DAQ framework. By providing the driver, it allows the dune DAQ software that has already ben developed to receive Eternet packets to also receive packets from the Xilinx U50, in principle simply by selecting this driver in the DPDKlibs in userspace.

Version 1 (under development here) uses two configurations to gain confidence in this early step.  
<table width=100%>
 <tr width=100%>
  <td>PHOBOS-XRTA is a system architecture inspired by accelertor cards in which we assume the data raw data are received in the computer from a standard NIC by DPDK into mbuf structures (a part of dpdk), the software sorts the packets if needed and sends them to the U50 in a way that would be plausible to do hit finding on them.  The data throughput that will be demonstrated is in the wrong direction, i.e. host-to-card rather than card-to-host.  The firmware used to test the U50 has been developed from the Xilinx vitis-tutorials.</td>
  <td>PHOBOS-XRTN adopts a system architecture that can demonstrate the PCIe and software throughput in our preferred mode of using the U50, with the full bandwidth flowing from card-to-host.  Fake data packets are generated for both the raw data and triggered hits.  They are sent to the softare in two separate streams.  The same vitis-tutorials example is adapted to provide this test data in the firmware</td>
</tr>
</table>

Software and firmware source code is organised under this directory in the git repository.  It contains a section to make the Vitis Kernel xrta.xclbin and xrtn.xclbin asdescribed below, and sections that are to be copied into the DPDK source tree to make the drivers and example code.   For now, the code runs entirely within DPDK and does not yet use the DUNE software environment.  

## Making the xclbin files

Will add bespoke instructions here.  Starting from the VITIS/xrta and VITIS/xrtn directories in this distribution, follow the instructions in github.com/Xilinx/Vitis-Tutorials/Hardware-acceleration/Features/08-xxxxx to install Vitis, XRT and the U50 platform files and then type 'make' in each of these directories.  The vitis install installs a very large amount of software, and if you already have vivado/vitis installed, there may be a trick to use that.  make will take about 2 hours for each of xrta and xrtn.  It may be that the xclbin files I have made can be copied across to your machine to avoid all of this.

Note that when you install the XRT libraries, when the machine boots, Linux will now bind the U50 to the XRT driver (which is a kernel driver).  If you are running with xdma some of the time, you may need to swap between these two drivers somehow.

## Making and running the DPDK examples

Copy the directories DPDK/drivers/net/xrta and DPDK/drivers/net/xrtn into the drivers/net directory substructure in your DPDK installation.  Copy the DPDK/examples/xrta and DPDK/examples/xrtn into the examples directory in your DPDK installation.  These were tested with DPDK version 21.11 LTS.  Modify the drivers/net/meson.build and examples/meson.build files in your DPDK installation to allow them to visi the directories you have just copied in.  This can be done in analogy to the meson.build files in the repository (which are from DPDK version 21.11 LTS).  There is one line to add for each directory you added, i.e. two lines in each meson.buold file.  Note that there is a trailing comma insode the list in the mesobn.build file.

TODO: There will be a line to add to expose the xrt library in a meson.buold file somewhere

The source code should now build with these additional drivers and examples.

TODO: Give examples of how to run it.  parameters and EAL

TODO: Give examples of the performance.


