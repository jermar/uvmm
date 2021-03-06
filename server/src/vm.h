/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include "cpu_dev_array.h"
#include "device.h"
#include "device_repo.h"
#include "guest.h"
#include "vm_ram.h"
#include "virt_bus.h"

namespace Vmm {

/**
 * The main instance of a hardware-virtualized guest.
 */
class Vm : public Vdev::Device_lookup
{
public:
  cxx::Ref_ptr<Vdev::Device>
  device_from_node(Vdev::Dt_node const &node) const override
  { return _devices.device_from_node(node); }

  Vmm::Guest *vmm() const override
  { return _vmm; }

  cxx::Ref_ptr<Vmm::Vm_ram> ram() const override
  { return _ram; }

  cxx::Ref_ptr<Vmm::Virt_bus> vbus() const override
  { return _vbus; }

  cxx::Ref_ptr<Vmm::Cpu_dev_array> cpus() const override
  { return _cpus; }

  /**
   * \see Device_lookup::get_or_create_ic_dev(Vdev::Dt_node const &node,
   *                                          bool fatal)
   */
  cxx::Ref_ptr<Gic::Ic> get_or_create_ic_dev(Vdev::Dt_node const &node,
                                             bool fatal) override;
  /**
   * \see Device_lookup::get_or_create_ic(Vdev::Dt_node const &node,
   *                                      cxx::Ref_ptr<Gic::Ic> *ic_ptr)
   */
  Ic_error get_or_create_ic(Vdev::Dt_node const &node,
                            cxx::Ref_ptr<Gic::Ic> *ic_ptr) override;

  /**
   * \see Device_lookup::get_or_create_mc_dev()
   */
  cxx::Ref_ptr<Gic::Msi_controller>
  get_or_create_mc_dev(Vdev::Dt_node const &node) override;

  void create_default_devices()
  {
    _vmm = Vmm::Guest::create_instance();
    _ram = Vdev::make_device<Vmm::Vm_ram>(Vmm::Guest::Boot_offset);

    auto vbus_cap = L4Re::Env::env()->get_cap<L4vbus::Vbus>("vbus");
    _vbus = cxx::make_ref_obj<Vmm::Virt_bus>(vbus_cap);

    _cpus = Vdev::make_device<Vmm::Cpu_dev_array>();
  }

  void add_device(Vdev::Dt_node const &node,
                  cxx::Ref_ptr<Vdev::Device> dev) override
  { _devices.add(node, dev); }

  /**
   * Find MSI parent of node.
   *
   * \param node  Node to find the MSI parent of.
   *
   * \return  The node of the MSI parent or an invalid node, if the 'msi-parent'
   *          property is not specified or references an invalid node.
   *
   * \note  Currently, this function only returns the simple case of one
   *        referenced MSI parent node in the device tree.
   */
  Vdev::Dt_node find_msi_parent(Vdev::Dt_node const &node) const
  {
    int size = 0;
    auto *prop = node.get_prop<fdt32_t>("msi-parent", &size);

    if (size > 1)
      L4Re::chksys(-L4_EINVAL,
                   "MSI parent is a single reference without sideband data.");

    return (prop && size > 0) ? node.find_phandle(prop)
                              : Vdev::Dt_node();
  }

  /**
   * Collect and instantiate all devices described in the device tree.
   *
   * \param dt  Device tree to scan.
   *
   * This function first instantiates all device for which a virtual
   * implementation exists and then goes through the remaining devices
   * and tries to assign any missing resources from the vbus (if existing).
   */
  void scan_device_tree(Vdev::Device_tree dt);

private:
  bool add_virt_device(Vdev::Dt_node const &node);
  bool add_phys_device(Vdev::Dt_node const &node);
  bool add_phys_device_by_vbus_id(Vdev::Dt_node const &node);

  Vdev::Device_repository _devices;
  Vmm::Guest *_vmm;
  cxx::Ref_ptr<Vmm::Vm_ram> _ram;
  cxx::Ref_ptr<Vmm::Virt_bus> _vbus;
  cxx::Ref_ptr<Vmm::Cpu_dev_array> _cpus;
};
}
