/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Philipp Eppelt <philipp.eppelt@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <mutex>
#include <l4/cxx/bitfield>
#include <l4/util/rdtsc.h>

#include "device.h"
#include "io_device.h"
#include "irq.h"
#include "timer.h"

namespace Vdev {

/**
 * Limited implementation of x86 PIT.
 *
 * Supports only channel 0 and 2.
 */
class Pit_timer
: public Vmm::Io_device,
  public Vdev::Device
{
  struct Port61 : public Vmm::Io_device
  {
    l4_uint8_t val = 0;
    void io_in(unsigned, Vmm::Mem_access::Width, l4_uint32_t *value) override
    { *value = val; }

    void io_out(unsigned, Vmm::Mem_access::Width, l4_uint32_t value) override
    { val = value & 0xff; }

    bool channel_2_on() const { return val & 0x1; }
    void set_output() { val |= (1 << 5); }
  };

  enum
  {
    Pit_tick_rate = 1193182,
    Channel_0_data = 0,
    Channel_2_data = 2,
    Mode_command = 3,

    Low_byte_mask = 0xff,
    High_byte_mask = 0xff00,
    High_byte_shift = 0x8,
    Latch_cmd_null_mask = 0x3f,
    Latch_cmd_channel_mask = 0xc0,

    Read_back_cmd = 3,
    Read_back_latch_0 = 1,
    Read_back_latch_2 = (1 << 2),
    Read_back_latch_cnt = 2,
    Access_lobyte = 1,
    Access_hibyte = 2,
    Access_lohi = 3,
  };

  struct Mode
  {
    l4_uint8_t raw;
    CXX_BITFIELD_MEMBER(6, 7, channel, raw);
    CXX_BITFIELD_MEMBER(4, 5, access, raw);
    CXX_BITFIELD_MEMBER(1, 3, opmode, raw);
    CXX_BITFIELD_MEMBER(0, 0, bcd, raw);
  };

  bool is_latch_count_value_cmd(Mode m) const
  {
    return !(m.raw & Latch_cmd_null_mask);
  }

  bool is_current_channel(Mode m, int port) const
  {
    return m.channel() == port;
  }

  static constexpr int port2idx(int port) { return port >> 1; }

  static Dbg trace() { return Dbg(Dbg::Irq, Dbg::Trace, "PIT"); }
  static Dbg warn() { return Dbg(Dbg::Irq, Dbg::Warn, "PIT"); }

public:
  Pit_timer(Gic::Ic *ic, int irq);
  virtual ~Pit_timer() = default;

  cxx::Ref_ptr<Vmm::Io_device> const port61() const { return _port61; }

  void io_out(unsigned port, Vmm::Mem_access::Width width,
              l4_uint32_t value) override;
  void io_in(unsigned port, Vmm::Mem_access::Width width,
             l4_uint32_t *value) override;

private:
  Vmm::Irq_edge_sink _irq;
  l4_uint16_t _latch[2];
  l4_uint16_t _counter[2];
  l4_uint16_t _reload[2];
  l4_uint8_t _ch_mode[2];
  bool _read_high;
  bool _wait_for_high_byte;
  Mode _mode;
  std::mutex _mutex;
  cxx::Ref_ptr<Port61> const _port61;
  l4_cpu_time_t _tsc_start[2];

  void set_high_byte(l4_uint16_t &reg, l4_uint8_t value);
  void set_low_byte(l4_uint16_t &reg, l4_uint8_t value);
};

} // namespace Vdev
