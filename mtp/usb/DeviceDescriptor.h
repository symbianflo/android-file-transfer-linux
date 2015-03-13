#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <libusb.h>
#include <mtp/types.h>
#include <mtp/usb/Device.h>
#include <mtp/usb/Interface.h>

namespace mtp { namespace usb
{
	class Configuration : Noncopyable
	{
		libusb_config_descriptor *_config;

	public:
		Configuration(libusb_config_descriptor *config) : _config(config) { }
		~Configuration() { libusb_free_config_descriptor(_config); }

		int GetIndex() const
		{ return _config->bConfigurationValue; }

		int GetInterfaceCount() const
		{ return _config->bNumInterfaces; }

		int GetInterfaceAltSettingsCount(int idx) const
		{ return _config->interface[idx].num_altsetting; }

		InterfacePtr GetInterface(ConfigurationPtr config, int idx, int settings) const
		{ return std::make_shared<Interface>(config, _config->interface[idx].altsetting[settings]); }
	};

	class DeviceDescriptor
	{
	private:
		libusb_device			*	_dev;
		libusb_device_descriptor	_descriptor;

	public:
		DeviceDescriptor(libusb_device *dev);
		~DeviceDescriptor();

		u16 GetVendorId() const
		{ return _descriptor.idVendor; }

		u16 GetProductId() const
		{ return _descriptor.idProduct; }

		DevicePtr Open(ContextPtr context);
		DevicePtr TryOpen(ContextPtr context);

		int GetConfigurationsCount() const
		{ return _descriptor.bNumConfigurations; }

		ConfigurationPtr GetConfiguration(int conf);
	};
	DECLARE_PTR(DeviceDescriptor);

}}

#endif

