#include <cli/Session.h>
#include <cli/CommandLine.h>
#include <cli/PosixStreams.h>
#include <cli/Tokenizer.h>

#include <mtp/make_function.h>

#include <stdio.h>

namespace cli
{

	Session::Session(const mtp::DevicePtr &device):
		_device(device),
		_session(_device->OpenSession(1)),
		_gdi(_session->GetDeviceInfo()),
		_cd(mtp::Session::Root),
		_running(true)
	{
		using namespace mtp;
		using namespace std::placeholders;

		printf("%s\n", _gdi.VendorExtensionDesc.c_str());
		printf("%s ", _gdi.Manufacturer.c_str());
		printf("%s ", _gdi.Model.c_str());
		printf("%s ", _gdi.DeviceVersion.c_str());
		//printf("%s", _gdi.SerialNumber.c_str());
		printf("\n");
		printf("supported op codes: ");
		for(OperationCode code : _gdi.OperationsSupported)
		{
			printf("%04x ", (unsigned)code);
		}
		printf("\n");
		printf("supported properties: ");
		for(u16 code : _gdi.DevicePropertiesSupported)
		{
			printf("%04x ", (unsigned)code);
		}
		printf("\n");

		AddCommand("help", "shows this help",
			make_function([this]() -> void { Help(); }));

		AddCommand("ls", "lists current directory",
			make_function([this]() -> void { List(); }));
		AddCommand("ls", "<path> lists objects in <path>",
			make_function([this](const Path &path) -> void { List(path); }));

		AddCommand("put", "<file> uploads file",
			make_function([this](const LocalPath &path) -> void { Put(path); }));

		AddCommand("put", "put <file> <dir> uploads file to directory",
			make_function([this](const LocalPath &path, const Path &dst) -> void { Put(path, dst); }));

		AddCommand("get", "<file> downloads file",
			make_function([this](const Path &path) -> void { Get(path); }));
		AddCommand("get", "<file> <dst> downloads file to <dst>",
			make_function([this](const Path &path, const LocalPath &dst) -> void { Get(dst, path); }));

		AddCommand("quit", "quits program",
			make_function([this]() -> void { Quit(); }));
		AddCommand("exit", "exits program",
			make_function([this]() -> void { Quit(); }));

		AddCommand("cd", "<path> change directory to <path>",
			make_function([this](const Path &path) -> void { ChangeDirectory(path); }));
		AddCommand("rm", "<path> removes object (WARNING: RECURSIVE, be careful!)",
			make_function([this](const LocalPath &path) -> void { Delete(path); }));
		AddCommand("mkdir", "<path> makes directory",
			make_function([this](const Path &path) -> void { MakeDirectory(path); }));

		AddCommand("storage-list", "shows available MTP storages",
			make_function([this]() -> void { ListStorages(); }));
		AddCommand("device-properties", "shows device's MTP properties",
			make_function([this]() -> void { ListDeviceProperties(); }));
	}

	char ** Session::CompletionCallback(const char *text, int start, int end)
	{
		Tokens tokens;
		Tokenizer(CommandLine::Get().GetLineBuffer(), tokens);
		if (tokens.size() < 2) //0 or 1
		{
			std::string command = !tokens.empty()? tokens.back(): std::string();
			char **comp = static_cast<char **>(calloc(sizeof(char *), _commands.size() + 1));
			auto it = _commands.begin();
			size_t i = 0, n = _commands.size();
			for(; n--; ++it)
			{
				if (end != 0 && it->first.compare(0, end, command) != 0)
					continue;

				comp[i++] = strdup(it->first.c_str());
			}
			if (i == 0) //readline silently dereference matches[0]
			{
				free(comp);
				comp = NULL;
			};
			return comp;
		}
		else
		{
			//completion
			const std::string &command = tokens.front();
			auto b = _commands.lower_bound(command);
			auto e = _commands.upper_bound(command);
			if (b == e)
				return NULL;

			decltype(b) i;
			for(i = b; i != e; ++i)
			{
				if (tokens.size() <= 1 + i->second->GetArgumentCount())
					break;
			}
			if (i == e)
				return NULL;
			//printf("COMPLETING %s with %u args\n", command.c_str(), i->second->GetArgumentCount());
		}
		return NULL;
	}

	void Session::ProcessCommand(const std::string &input)
	{
		Tokens tokens;
		Tokenizer(input, tokens);
		if (!tokens.empty())
			ProcessCommand(std::move(tokens));
	}

	void Session::ProcessCommand(Tokens && tokens_)
	{
		Tokens tokens(tokens_);
		if (tokens.empty())
			throw std::runtime_error("no token passed to ProcessCommand");

		std::string cmdName = tokens.front();
		tokens.pop_front();
		auto cmd = _commands.find(cmdName);
		if (cmd == _commands.end())
			throw std::runtime_error("invalid command " + cmdName);

		cmd->second->Execute(tokens);
	}

	void Session::InteractiveInput()
	{
		using namespace mtp;
		std::string prompt(_gdi.Manufacturer + " " + _gdi.Model + "> "), input;
		cli::CommandLine::Get().SetCallback([this](const char *text, int start, int end) -> char ** { return CompletionCallback(text, start, end); });

		while (cli::CommandLine::Get().ReadLine(prompt, input))
		{
			try
			{
				ProcessCommand(input);
				if (!_running) //do not put newline
					return;
			}
			catch(const std::exception &ex)
			{ printf("error: %s\n", ex.what()); }
		}
		printf("\n");
	}

	mtp::u32 Session::Resolve(const Path &path)
	{
		mtp::u32 id = _cd;
		for(size_t p = 0; p < path.size(); )
		{
			size_t next = path.find('/', p);
			if (next == path.npos)
				next = path.size();

			std::string entity(path.substr(p, next - p));
			if (entity == ".")
			{ }
			else
			if (entity == "..")
			{
				id = _session->GetObjectIntegerProperty(id, mtp::ObjectProperty::ParentObject);
				if (id == 0)
					id = mtp::Session::Root;
			}
			else
			{
				auto objectList = _session->GetObjectHandles(mtp::Session::AllStorages, mtp::Session::AllFormats, id);
				bool found = false;
				for(auto object : objectList.ObjectHandles)
				{
					std::string name = _session->GetObjectStringProperty(object, mtp::ObjectProperty::ObjectFilename);
					if (name == entity)
					{
						id = object;
						found = true;
						break;
					}
				}
				if (!found)
					throw std::runtime_error("could not find " + entity + " in path " + path.substr(0, p));
			}
			p = next + 1;
		}
		return id;
	}

	void Session::List(mtp::u32 parent)
	{
		using namespace mtp;
		msg::ObjectHandles handles = _session->GetObjectHandles(mtp::Session::AllStorages, mtp::Session::AllFormats, parent);

		for(u32 objectId : handles.ObjectHandles)
		{
			try
			{
				msg::ObjectInfo info = _session->GetObjectInfo(objectId);
				printf("%-10u %04hx %10u %s %ux%u, %s\n", objectId, info.ObjectFormat, info.ObjectCompressedSize, info.Filename.c_str(), info.ImagePixWidth, info.ImagePixHeight, info.CaptureDate.c_str());
			}
			catch(const std::exception &ex)
			{
				printf("error: %s\n", ex.what());
			}
		}

	}

	void Session::ListStorages()
	{
		using namespace mtp;
		msg::StorageIDs list = _session->GetStorageIDs();
		for(size_t i = 0; i < list.StorageIDs.size(); ++i)
		{
			msg::StorageInfo si = _session->GetStorageInfo(list.StorageIDs[i]);
			printf("%08d volume: %s, description: %s\n", list.StorageIDs[i], si.VolumeLabel.c_str(), si.StorageDescription.c_str());
		}
	}

	void Session::Help()
	{
		printf("Available commands are:\n");
		for(auto i : _commands)
		{
			printf("\t%-20s %s\n", i.first.c_str(), i.second->GetHelpString().c_str());
		}
	}

	void Session::Get(const LocalPath &dst, mtp::u32 srcId)
	{
		_session->GetObject(srcId, std::make_shared<ObjectOutputStream>(dst));
	}

	void Session::Get(mtp::u32 srcId)
	{
		auto info = _session->GetObjectInfo(srcId);
		printf("filename = %s\n", info.Filename.c_str());
		Get(LocalPath(info.Filename), srcId);
	}

	void Session::Put(mtp::u32 parentId, const LocalPath &src)
	{
		using namespace mtp;
		msg::ObjectInfo oi;
		oi.Filename = src;
		oi.ObjectFormat = ObjectFormatFromFilename(src);

		std::shared_ptr<ObjectInputStream> objectInput(new ObjectInputStream(src));
		oi.SetSize(objectInput->GetSize());

		auto noi = _session->SendObjectInfo(oi, 0, parentId);
		printf("new object id = %u\n", noi.ObjectId);
		_session->SendObject(objectInput);
		printf("done\n");
	}

	void Session::MakeDirectory(mtp::u32 parentId, const std::string & name)
	{
		using namespace mtp;
		msg::ObjectInfo oi;
		oi.Filename = name;
		oi.ObjectFormat = ObjectFormat::Association;
		_session->SendObjectInfo(oi, 0, parentId);
	}

	void Session::Delete(mtp::u32 id)
	{
		_session->DeleteObject(id);
	}

	void Session::ListProperties(mtp::u32 id)
	{
		auto ops = _session->GetObjectPropsSupported(id);
		printf("properties supported: ");
		for(mtp::u16 prop: ops.ObjectPropCodes)
		{
			printf("%02x ", prop);
		}
		printf("\n");
	}

	void Session::ListDeviceProperties()
	{
		using namespace mtp;
		for(u16 code : _gdi.DevicePropertiesSupported)
		{
			if ((code & 0xff00) != 0x5000 )
				continue;
			printf("property code: %04x\n", (unsigned)code);
			ByteArray data = _session->GetDeviceProperty((mtp::DeviceProperty)code);
			HexDump("value", data);
		}
	}

}
