# ----------------------------------------------------------------------------
#     _____
#    /     \
#   /____   \____
#  / \===\   \==/
# /___\===\___\/  AVNET Design Resource Center
#      \======/         www.em.avnet.com/drc
#       \====/    
# ----------------------------------------------------------------------------
#  
#  Disclaimer:
#     Avnet, Inc. makes no warranty for the use of this code or design.
#     This code is provided  "As Is". Avnet, Inc assumes no responsibility for
#     any errors, which may appear in this code, nor does it make a commitment
#     to update the information contained herein. Avnet, Inc specifically
#     disclaims any implied warranties of fitness for a particular purpose.
#                      Copyright(c) 2009 Avnet, Inc.
#                              All rights reserved.
# 
# ----------------------------------------------------------------------------

## System Clock 
# LVDS Programmable Clock Generator (CDCM61002)
#set_property  PACKAGE_PIN M5   [get_ports LVDS_CLK0_N]
#set_property  PACKAGE_PIN M6   [get_ports LVDS_CLK0_P]
#set_property  PACKAGE_PIN P5   [get_ports LVDS_CLK1_N]
#set_property  PACKAGE_PIN P6   [get_ports LVDS_CLK1_P]

#set_property  IOSTANDARD LVDS [get_ports LVDS_CLK0_N]
#set_property  IOSTANDARD LVDS [get_ports LVDS_CLK0_P]
#set_property  IOSTANDARD LVDS [get_ports LVDS_CLK1_N]
#set_property  IOSTANDARD LVDS [get_ports LVDS_CLK1_P]

##DDR clocks
set_property PACKAGE_PIN H22 [get_ports c0_sys_clk_clk_p]
set_property PACKAGE_PIN H23 [get_ports c0_sys_clk_clk_n]
set_property IOSTANDARD DIFF_SSTL12 [get_ports c0_sys_clk_clk_p]
set_property IOSTANDARD DIFF_SSTL12 [get_ports c0_sys_clk_clk_n]

create_clock -period 4.000 [get_ports c0_sys_clk_clk_p]

#derive_pll_clocks
#derive_clock_uncertainty

set_property CFGBVS VCCO [current_design]

#Constraint Clock Transitions
#RX_CLK -> sys_clk
# RX_CLK is 25MHz for 100Mbps operation according to # Texas Instruments DP83867 
# Datasheet
create_clock -period 40 [get_ports {ENET_RX_CLK}]
# Ethernet Core has only RX_CLK -> system clock and TX_CLK -> system clock 
# transitions. RX_CLK and TX_CLK have the same source 
# (see top_system_eth_template.vh)
set_max_delay -from [get_clocks {ENET_RX_CLK}] -to [get_clocks {mmcm_clkout1}] 100

## USB-UART Interface
set_property PACKAGE_PIN D20 [get_ports uart_txd]
set_property IOSTANDARD LVCMOS18 [get_ports uart_txd]
set_property PACKAGE_PIN C19 [get_ports uart_rxd]
set_property IOSTANDARD LVCMOS18 [get_ports uart_rxd]

###### User LEDs
#set_property PACKAGE_PIN D16 [get_ports {led[6]}]
#set_property IOSTANDARD LVCMOS18 [get_ports {led[6]}]

#set_property PACKAGE_PIN G16 [get_ports {led[5]}]
#set_property IOSTANDARD LVCMOS18 [get_ports {led[5]}]

#set_property PACKAGE_PIN H16 [get_ports {led[4]}]
#set_property IOSTANDARD LVCMOS18 [get_ports {led[4]}]

#set_property PACKAGE_PIN E18 [get_ports {led[3]}]
#set_property IOSTANDARD LVCMOS18 [get_ports {led[3]}]

#set_property PACKAGE_PIN E17 [get_ports {led[2]}]
#set_property IOSTANDARD LVCMOS18 [get_ports {led[2]}]

#set_property PACKAGE_PIN E16 [get_ports {led[1]}]
#set_property IOSTANDARD LVCMOS18 [get_ports {led[1]}]

#set_property PACKAGE_PIN H18 [get_ports {led[0]}]
#set_property IOSTANDARD LVCMOS18 [get_ports {led[0]}]

set_property PACKAGE_PIN H17 [get_ports {trap}]
set_property IOSTANDARD LVCMOS18 [get_ports {trap}]

####### User PUSH Switches
set_property PACKAGE_PIN K20 [get_ports {reset}]
set_property IOSTANDARD LVCMOS12 [get_ports {reset}]

#set_property PACKAGE_PIN K18 [get_ports {gpio_push_sw_tri_i[0]}]
#set_property IOSTANDARD LVCMOS12 [get_ports {gpio_push_sw_tri_i[0]}]

#set_property PACKAGE_PIN L18 [get_ports {gpio_push_sw_tri_i[1]}]
#set_property IOSTANDARD LVCMOS12 [get_ports {gpio_push_sw_tri_i[1]}]

#set_property PACKAGE_PIN K21 [get_ports {gpio_push_sw_tri_i[2]}]
#set_property IOSTANDARD LVCMOS12 [get_ports {gpio_push_sw_tri_i[2]}]

#set_property PACKAGE_PIN K20 [get_ports {gpio_push_sw_tri_i[3]}]
#set_property IOSTANDARD LVCMOS12 [get_ports {gpio_push_sw_tri_i[3]}]

## Ethernet #1 Interface (J1)
set_property PACKAGE_PIN D9 [get_ports ENET_RESETN]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_RESETN]

set_property PACKAGE_PIN A10 [get_ports ENET_RX_D0]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_RX_D0]

set_property PACKAGE_PIN B10 [get_ports ENET_RX_D1]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_RX_D1]

set_property PACKAGE_PIN B11 [get_ports ENET_RX_D2]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_RX_D2]

set_property PACKAGE_PIN C11 [get_ports ENET_RX_D3]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_RX_D3]

set_property PACKAGE_PIN D11 [get_ports ENET_RX_DV]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_RX_DV]

set_property PACKAGE_PIN E11 [get_ports ENET_RX_CLK]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_RX_CLK]

set_property PACKAGE_PIN H8 [get_ports ENET_TX_D0]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_TX_D0]

set_property PACKAGE_PIN H9 [get_ports ENET_TX_D1]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_TX_D1]

set_property PACKAGE_PIN J9 [get_ports ENET_TX_D2]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_TX_D2]

set_property PACKAGE_PIN J10 [get_ports ENET_TX_D3]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_TX_D3]

set_property PACKAGE_PIN G9 [get_ports ENET_TX_EN]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_TX_EN]

set_property PACKAGE_PIN G10 [get_ports ENET_GTX_CLK]
set_property IOSTANDARD LVCMOS18 [get_ports ENET_GTX_CLK]

set_property IOB TRUE [get_ports ENET_TX_D0]
set_property IOB TRUE [get_ports ENET_TX_D1]
set_property IOB TRUE [get_ports ENET_TX_D2]
set_property IOB TRUE [get_ports ENET_TX_D3]
set_property IOB TRUE [get_ports ENET_TX_EN]
