/*
 * Copyright (C) 2016 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <string>
#include <vector>

#include "device.h"
#include "device_tree.h"

namespace Vdev {

class Device_repository
{
  struct Dt_device
  {
    std::string path;
    l4_uint32_t phandle;
    cxx::Ref_ptr<Device> dev;
  };

public:
  cxx::Ref_ptr<Device> device_from_node(Dt_node const &node) const
  {
    l4_uint32_t phandle = node.get_phandle();

    if (phandle != 0 && phandle != -1U)
      {
        for (auto const &d : _devices)
          {
            if (d.phandle == phandle)
              return d.dev;
          }
      }

    char buf[1024];
    node.get_path(buf, sizeof(buf));

    for (auto const &d : _devices)
      {
        if (d.path == buf)
          return d.dev;
      }

    return cxx::Ref_ptr<Device>();
  }

  void add(Dt_node const &node, cxx::Ref_ptr<Device> dev)
  {
    char buf[1024];
    l4_uint32_t phandle = node.get_phandle();
    node.get_path(buf, sizeof(buf));

    _devices.push_back({buf, phandle, dev});
  }

private:
  std::vector<Dt_device> _devices;
};

} // namespace
