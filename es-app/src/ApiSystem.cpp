#include "ApiSystem.h"
#include <stdlib.h>
#if !defined(WIN32)
#include <sys/statvfs.h>
#endif
#include <sstream>
#include "Settings.h"
#include <iostream>
#include <fstream>
#include "Log.h"
#include "HttpReq.h"
#include <chrono>
#include <thread>

#include "AudioManager.h"
#include "VolumeControl.h"
#include "InputManager.h"
#include <SystemConf.h>

#include <stdio.h>
#include <string.h>
#include <regex>
#include <sys/types.h>
#include <algorithm>

#if !defined(WIN32)
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#endif

#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include <fstream>
#include <SDL.h>
#include <Sound.h>
#include "utils/ThreadPool.h"

#if WIN32
#include <Windows.h>
#define popen _popen
#define pclose _pclose
#define WIFEXITED(x) x
#define WEXITSTATUS(x) x
#include "Win32ApiSystem.h"
#endif

#include <pugixml.hpp>
#include "platform.h"
#include "RetroAchievements.h"
#include "utils/ZipFile.h"

ApiSystem::ApiSystem() { }

ApiSystem* ApiSystem::instance = nullptr;

ApiSystem *ApiSystem::getInstance() 
{
	if (ApiSystem::instance == nullptr)
#if WIN32
		ApiSystem::instance = new Win32ApiSystem();
#else
		ApiSystem::instance = new ApiSystem();
#endif

	return ApiSystem::instance;
}

unsigned long ApiSystem::getFreeSpaceGB(std::string mountpoint) 
{
	LOG(LogDebug) << "ApiSystem::getFreeSpaceGB";

	int free = 0;

#if !WIN32
	struct statvfs fiData;
	if ((statvfs(mountpoint.c_str(), &fiData)) >= 0)
		free = (fiData.f_bfree * fiData.f_bsize) / (1024 * 1024 * 1024);
#endif

	return free;
}

std::string ApiSystem::getFreeSpaceUserInfo() {
  return getFreeSpaceInfo("/storage/roms");
}

std::string ApiSystem::getFreeSpaceSystemInfo() {
  return getFreeSpaceInfo("/storage");
}

std::string ApiSystem::getFreeSpaceInfo(const std::string mountpoint)
{
	LOG(LogDebug) << "ApiSystem::getFreeSpaceInfo";

	std::ostringstream oss;

#if !WIN32
	struct statvfs fiData;
	if ((statvfs(mountpoint.c_str(), &fiData)) < 0)
		return "";
		
	unsigned long long total = (unsigned long long) fiData.f_blocks * (unsigned long long) (fiData.f_bsize);
	unsigned long long free = (unsigned long long) fiData.f_bfree * (unsigned long long) (fiData.f_bsize);
	unsigned long long used = total - free;
	unsigned long percent = 0;
	
	if (total != 0) 
	{  //for small SD card ;) with share < 1GB
		percent = used * 100 / total;
		oss << Utils::FileSystem::megaBytesToString(used / (1024L * 1024L)) << "/" << Utils::FileSystem::megaBytesToString(total / (1024L * 1024L)) << " (" << percent << "%)";
	}
	else
		oss << "N/A";	
#endif

	return oss.str();
}

bool ApiSystem::isFreeSpaceLimit() 
{
	return getFreeSpaceGB("/storage/roms") < 2;
}

std::string ApiSystem::getVersion() 
{
	std::ifstream ifs("/storage/.config/.OS_VERSION");
	if (ifs.good()) 
	{
		std::string contents;
		std::getline(ifs, contents);
		return contents;
	}

	return "";
}

std::string ApiSystem::getApplicationName()
{
	std::ifstream ifs("/storage/.config/.OS_BUILD_DATE");
	if (ifs.good()) 
	{
		std::string contents;
		std::getline(ifs, contents);
		return "EmulationStation (" + contents + ")";
	}

	return "EmulationStation";
}

bool ApiSystem::setOverscan(bool enable) 
{
	return executeScriptLegacy("batocera-config overscan " + std::string(enable ? "enable" : "disable"));
}

// BusyComponent* ui
std::pair<std::string, int> ApiSystem::updateSystem(const std::function<void(const std::string)>& func)
{
	LOG(LogDebug) << "ApiSystem::updateSystem";

	std::string updatecommand = "run system-upgrade";

	FILE *pipe = popen(updatecommand.c_str(), "r");
	if (pipe == nullptr)
		return std::pair<std::string, int>(std::string("Cannot call update command"), -1);

	char line[1024] = "";
	FILE *flog = fopen("/var/log/system-upgrade.log", "w");
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		if (flog != nullptr) 
			fprintf(flog, "%s\n", line);

		if (func != nullptr)
			func(std::string(line));		
	}

	int exitCode = WEXITSTATUS(pclose(pipe));

	if (flog != NULL)
	{
		fprintf(flog, "Exit code : %d\n", exitCode);
		fclose(flog);
	}

	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::pair<std::string, int> ApiSystem::backupSystem(BusyComponent* ui, std::string device) 
{
	LOG(LogDebug) << "ApiSystem::backupSystem";

	std::string updatecommand = "batocera-sync sync " + device;
	FILE* pipe = popen(updatecommand.c_str(), "r");
	if (pipe == NULL)
		return std::pair<std::string, int>(std::string("Cannot call sync command"), -1);

	char line[1024] = "";

	FILE* flog = fopen("/var/log/sync.log", "w");
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");

		if (flog != NULL) 
			fprintf(flog, "%s\n", line);

		ui->setText(std::string(line));
	}

	if (flog != NULL) 
		fclose(flog);

	int exitCode = WEXITSTATUS(pclose(pipe));
	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::pair<std::string, int> ApiSystem::installSystem(BusyComponent* ui, std::string device, std::string architecture) 
{
	LOG(LogDebug) << "ApiSystem::installSystem";

	std::string updatecommand = "batocera-install install " + device + " " + architecture;
	FILE *pipe = popen(updatecommand.c_str(), "r");
	if (pipe == NULL)
		return std::pair<std::string, int>(std::string("Cannot call install command"), -1);

	char line[1024] = "";

	FILE *flog = fopen("/var/log/install.log", "w");
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		if (flog != NULL) fprintf(flog, "%s\n", line);
		ui->setText(std::string(line));
	}

	int exitCode = WEXITSTATUS(pclose(pipe));

	if (flog != NULL)
	{
		fprintf(flog, "Exit code : %d\n", exitCode);
		fclose(flog);
	}

	return std::pair<std::string, int>(std::string(line), exitCode);
}

std::pair<std::string, int> ApiSystem::scrape(BusyComponent* ui) 
{
	LOG(LogDebug) << "ApiSystem::scrape";

	FILE* pipe = popen("batocera-scraper", "r");
	if (pipe == nullptr)
		return std::pair<std::string, int>(std::string("Cannot call scrape command"), -1);

	char line[1024] = "";

	FILE* flog = fopen("/tmp/logs/351elec-scraper.log", "w");
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");

		if (flog != NULL) 
			fprintf(flog, "%s\n", line);

		if (ui != nullptr && Utils::String::startsWith(line, "GAME: "))
			ui->setText(std::string(line));	
	}

	if (flog != nullptr)
		fclose(flog);

	int exitCode = WEXITSTATUS(pclose(pipe));
	return std::pair<std::string, int>(std::string(line), exitCode);
}

bool ApiSystem::ping() 
{
	if (!executeScriptLegacy("timeout 1 ping -c 1 -t 255 -w 1 8.8.8.8")) // ping Google DNS
		return executeScriptLegacy("timeout 2 ping -c 1 -t 255 -w 1 8.8.4.4"); // ping Google secondary DNS & give 2 seconds

	return true;
}

bool ApiSystem::canUpdate(std::vector<std::string>& output) 
{
#ifdef _ENABLEUPDATES
	LOG(LogDebug) << "ApiSystem::canUpdate";

	FILE *pipe = popen("updatecheck", "r");
	if (pipe == NULL)
		return false;

	char line[1024];
	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		output.push_back(std::string(line));
	}

	int res = WEXITSTATUS(pclose(pipe));
        bool ForceUpdateEnabled = SystemConf::getInstance()->getBool("updates.force");
	if (res == 0 || ForceUpdateEnabled == true) 
	{
		LOG(LogInfo) << "Can update ";
		return true;
	}

	LOG(LogInfo) << "Cannot update ";
#endif
	return false;
}

void ApiSystem::launchExternalWindow_before(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchExternalWindow_before";

	AudioManager::getInstance()->deinit();
	VolumeControl::getInstance()->deinit();
#ifdef _ENABLEEMUELEC	
	window->deinit(false);
#else
	window->deinit();
#endif
	LOG(LogDebug) << "ApiSystem::launchExternalWindow_before OK";
}

void ApiSystem::launchExternalWindow_after(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchExternalWindow_after";
#ifdef _ENABLEEMUELEC
	window->init(false);
#else
	window->init();
#endif
	VolumeControl::getInstance()->init();
	AudioManager::getInstance()->init();
	window->normalizeNextUpdate();
	window->reactivateGui();

	AudioManager::getInstance()->playRandomMusic();

	LOG(LogDebug) << "ApiSystem::launchExternalWindow_after OK";
}

bool ApiSystem::launchKodi(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchKodi";

	std::string commandline = InputManager::getInstance()->configureEmulators();
	std::string command = "batocera-kodi " + commandline;

	ApiSystem::launchExternalWindow_before(window);

	int exitCode = system(command.c_str());

	// WIFEXITED returns a nonzero value if the child process terminated normally with exit or _exit.
	// https://www.gnu.org/software/libc/manual/html_node/Process-Completion-Status.html
	if (WIFEXITED(exitCode))
		exitCode = WEXITSTATUS(exitCode);

	ApiSystem::launchExternalWindow_after(window);

	// handle end of kodi
	switch (exitCode) 
	{
	case 10: // reboot code
		quitES(QuitMode::REBOOT);		
		return true;
		
	case 11: // shutdown code
		quitES(QuitMode::SHUTDOWN);		
		return true;
	}

	return exitCode == 0;
}

bool ApiSystem::launchFileManager(Window *window) 
{
	LOG(LogDebug) << "ApiSystem::launchFileManager";

	std::string command = "filemanagerlauncher";

	ApiSystem::launchExternalWindow_before(window);

	int exitCode = system(command.c_str());
	if (WIFEXITED(exitCode))
		exitCode = WEXITSTATUS(exitCode);

	ApiSystem::launchExternalWindow_after(window);

	return exitCode == 0;
}

bool ApiSystem::enableWifi(std::string ssid, std::string key) 
{
	// Escape single quote if it's in the passphrase
	using std::regex;
        using std::regex_replace;

	regex tic("(')");
	key = regex_replace(key,tic,"\\'");
        ssid = regex_replace(ssid,tic,"\\'");
	return executeScriptLegacy("/usr/bin/wifictl enable $\'" + ssid + "\' $\'" + key + "\'");
}

bool ApiSystem::disableWifi() 
{
	return executeScriptLegacy("/usr/bin/wifictl disable");
}

std::string ApiSystem::getIpAdress() 
{
	LOG(LogDebug) << "ApiSystem::getIpAdress";
	
	std::string result = queryIPAdress(); // platform.h
	if (result.empty())
		return "NOT CONNECTED";

	return result;
}

unsigned long ApiSystem::GetTotalRam()
{
    return executeScriptLegacy("echo $(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1000 ))");
}

bool ApiSystem::scanNewBluetooth(const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-bluetooth trust", func).second == 0;
}

std::vector<std::string> ApiSystem::getBluetoothDeviceList()
{
	return executeEnumerationScript("batocera-bluetooth list");
}

bool ApiSystem::removeBluetoothDevice(const std::string deviceName)
{
	return executeScriptLegacy("batocera-bluetooth remove "+ deviceName);
}

std::vector<std::string> ApiSystem::getAvailableStorageDevices() 
{
	return executeEnumerationScript("batocera-config storage list");
}

std::vector<std::string> ApiSystem::getVideoModes() 
{
	return executeEnumerationScript("batocera-resolution listModes");
}

std::vector<std::string> ApiSystem::getAvailableBackupDevices() 
{
	return executeEnumerationScript("batocera-sync list");
}

std::vector<std::string> ApiSystem::getAvailableInstallDevices() 
{
	return executeEnumerationScript("batocera-install listDisks");
}

std::vector<std::string> ApiSystem::getAvailableInstallArchitectures() 
{
	return executeEnumerationScript("batocera-install listArchs");
}

std::vector<std::string> ApiSystem::getAvailableOverclocking() 
{
#ifdef _ENABLEEMUELEC
	return executeEnumerationScript("echo no");
#else
	return executeEnumerationScript("batocera-overclock list");
#endif
}

std::vector<std::string> ApiSystem::getSystemInformations() 
{
	return executeEnumerationScript("batocera-info --full");
}

std::vector<BiosSystem> ApiSystem::getBiosInformations(const std::string system) 
{
	std::vector<BiosSystem> res;
	BiosSystem current;
	bool isCurrent = false;

	std::string cmd = "batocera-systems";
	if (!system.empty())
		cmd += " --filter " + system;

	auto systems = executeEnumerationScript(cmd);
	for (auto line : systems)
	{
		if (Utils::String::startsWith(line, "> ")) 
		{
			if (isCurrent)
				res.push_back(current);

			isCurrent = true;
			current.name = std::string(std::string(line).substr(2));
			current.bios.clear();
		}
		else 
		{
			BiosFile biosFile;
			std::vector<std::string> tokens = Utils::String::split(line, ' ');
			if (tokens.size() >= 3) 
			{
				biosFile.status = tokens.at(0);
				biosFile.md5 = tokens.at(1);

				// concatenat the ending words
				std::string vname = "";
				for (unsigned int i = 2; i < tokens.size(); i++) 
				{
					if (i > 2) vname += " ";
					vname += tokens.at(i);
				}
				biosFile.path = vname;

				current.bios.push_back(biosFile);
			}
		}
	}

	if (isCurrent)
		res.push_back(current);

	return res;
}

bool ApiSystem::generateSupportFile() 
{
	return executeScriptLegacy("batocera-support");
}

std::string ApiSystem::getCurrentStorage() 
{
	LOG(LogDebug) << "ApiSystem::getCurrentStorage";

#if WIN32
	return "DEFAULT";
#endif

	std::ostringstream oss;
	oss << "batocera-config storage current";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL)
		return "";	

	if (fgets(line, 1024, pipe)) {
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}
	return "INTERNAL";
}

bool ApiSystem::setStorage(std::string selected) 
{
	return executeScriptLegacy("batocera-config storage " + selected);
}

bool ApiSystem::setButtonColorGameForce(std::string selected)
{
	return executeScriptLegacy("batocera-gameforce buttonColorLed " + selected);
}

bool ApiSystem::setPowerLedGameForce(std::string selected)
{
	return executeScriptLegacy("batocera-gameforce powerLed " + selected);
}

bool ApiSystem::forgetBluetoothControllers() 
{
	return executeScriptLegacy("batocera-config forgetBT");
}

std::string ApiSystem::getRootPassword() 
{
	LOG(LogDebug) << "ApiSystem::getRootPassword";

	std::ostringstream oss;
	oss << "batocera-config getRootPassword";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL) {
		return "";
	}

	if (fgets(line, 1024, pipe)) {
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}
	return oss.str().c_str();
}

std::vector<std::string> ApiSystem::getAvailableVideoOutputDevices() 
{
	return executeEnumerationScript("batocera-config lsoutputs");
}

std::vector<std::string> ApiSystem::getAvailableAudioOutputDevices() 
{
#if WIN32
	std::vector<std::string> res;
	res.push_back("default");
	return res;
#endif

	return executeEnumerationScript("set-audio list");
}

std::vector<std::string> ApiSystem::getAvailableChannels()
{
        return executeEnumerationScript("/usr/bin/sh -lc \"/usr/bin/wifictl channels\"");
}

std::vector<std::string> ApiSystem::getAvailableThreads()
{
        return executeEnumerationScript("/usr/bin/sh -lc \". /etc/profile.d/099-freqfunctions; get_threads\"");
}

std::vector<std::string> ApiSystem::getAvailableGovernors()
{
	return executeEnumerationScript("/usr/bin/sh -lc \"tr \\\" \\\" \\\"\\n\\\" < /sys/devices/system/cpu/cpufreq/policy0/scaling_available_governors\" | grep [a-z]");
}

std::vector<std::string> ApiSystem::getSleepModes()
{
        return executeEnumerationScript("/usr/bin/sh -lc \"echo \\\"default\\\"; tr \\\" \\\" \\\"\\n\\\" </sys/power/state | grep -v disk\"");
}

std::vector<std::string> ApiSystem::getCPUVendor()
{
	return executeEnumerationScript("awk \'/vendor_id/ {print $3;exit}\' /proc/cpuinfo");
}

std::string ApiSystem::getCurrentAudioOutputDevice() 
{
#if WIN32
	return "default";
#endif

	LOG(LogDebug) << "ApiSystem::getCurrentAudioOutputDevice";

	std::ostringstream oss;
	oss << "set-audio get";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL)
		return "";	

	if (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}

	return "";
}

bool ApiSystem::setAudioOutputDevice(std::string selected) 
{
	LOG(LogDebug) << "ApiSystem::setAudioOutputDevice";

	std::ostringstream oss;

	oss << "set-audio set" << " '" << selected << "'";
	int exitcode = system(oss.str().c_str());

	Sound::get("/usr/share/emulationstation/resources/checksound.ogg")->play();

	return exitcode == 0;
}

std::vector<std::string> ApiSystem::getAvailableAudioOutputPaths()
{
#if WIN32
        std::vector<std::string> res;
        res.push_back("default");
        return res;
#endif

        return executeEnumerationScript("set-audio controls");
}

std::string ApiSystem::getCurrentAudioOutputPath()
{
#if WIN32
        return "default";
#endif

        LOG(LogDebug) << "ApiSystem::getCurrentAudioOutputPath";

        std::ostringstream oss;
        oss << "set-audio esget";
        FILE *pipe = popen(oss.str().c_str(), "r");
        char line[1024];

        if (pipe == NULL)
                return "";

        if (fgets(line, 1024, pipe))
        {
                strtok(line, "\n");
                pclose(pipe);
                return std::string(line);
        }

        return "";
}

bool ApiSystem::setAudioOutputPath(std::string selected)
{
        LOG(LogDebug) << "ApiSystem::setAudioOutputPath";

        std::ostringstream oss;

        oss << "set-audio esset" << " '" << selected << "'";
        int exitcode = system(oss.str().c_str());

        Sound::get("/usr/share/emulationstation/resources/checksound.ogg")->play();

        return exitcode == 0;
}

std::vector<std::string> ApiSystem::getAvailableAudioOutputProfiles()
{
#if WIN32
	std::vector<std::string> res;
	res.push_back("default");
	return res;
#endif

	return executeEnumerationScript("batocera-audio list-profiles");
}

std::string ApiSystem::getCurrentAudioOutputProfile() 
{
#if WIN32
	return "default";
#endif

	LOG(LogDebug) << "ApiSystem::getCurrentAudioOutputProfile";

	std::ostringstream oss;
	oss << "batocera-audio get-profile";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL)
		return "";	

	if (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}

	return "";
}

bool ApiSystem::setAudioOutputProfile(std::string selected) 
{
	LOG(LogDebug) << "ApiSystem::setAudioOutputProfile";

	std::ostringstream oss;

	oss << "batocera-audio set-profile" << " '" << selected << "'";
	int exitcode = system(oss.str().c_str());

	Sound::get("/usr/share/emulationstation/resources/checksound.ogg")->play();

	return exitcode == 0;
}

void ApiSystem::getThreeFiftyOnePackagesImages(std::vector<ThreeFiftyOnePackage>& items)
{

        std::vector<std::pair<std::vector<ThreeFiftyOnePackage>::iterator, HttpReq*>> requests;

        for (auto it = items.begin(); it != items.end(); ++it)
        {
                std::string distantFile = getUpdateUrl() + "/packages/" + it->name + ".jpg";
                it->image = distantFile;
                continue;


                std::string localPath = Utils::FileSystem::getGenericPath(Utils::FileSystem::getEsConfigPath() + "/tmp");
                if (!Utils::FileSystem::exists(localPath))
                        Utils::FileSystem::createDirectory(localPath);

                std::string localFile = localPath + "/" + it->name + ".jpg";

                if (Utils::FileSystem::exists(localFile))
                {
                        auto date = Utils::FileSystem::getFileCreationDate(localFile);
                        auto duration = Utils::Time::DateTime::now().elapsedSecondsSince(date);
                        if (duration > 86400) // 1 day
                                Utils::FileSystem::removeFile(localFile);
                }

                if (Utils::FileSystem::exists(localFile))
                        it->image = localFile;
                else
                {
                        HttpReq* httpreq = new HttpReq(distantFile, localFile);

                        auto pair = std::pair<std::vector<ThreeFiftyOnePackage>::iterator, HttpReq*>(it, httpreq);
                        requests.push_back(pair);
                }
        }

        bool running = true;

        while (running && requests.size() > 0)
        {
                running = false;

                for (auto it = requests.begin(); it != requests.end(); ++it)
                {
                        if (it->second->status() == HttpReq::REQ_IN_PROGRESS)
                        {
                                running = true;
                                continue;
                        }

                        if (it->second->status() == HttpReq::REQ_SUCCESS)
                        {
                                auto filePath = it->second->getFilePath();
                                if (Utils::FileSystem::exists(filePath))
                                        it->first->image = filePath;
                        }

                        delete it->second;
                        requests.erase(it);
                        running = true;
                        break;
                }
        }
}

std::vector<ThreeFiftyOnePackage> ApiSystem::getThreeFiftyOnePackagesList()
{
        LOG(LogDebug) << "ApiSystem::getThreeFiftyOnePackagesList";

        std::vector<ThreeFiftyOnePackage> res;

        std::string command = "351elec-es-packages list";
        FILE *pipe = popen(command.c_str(), "r");
        if (pipe == NULL)
                return res;

        char line[1024];
        char *pch;

        while (fgets(line, 1024, pipe))
        {
                strtok(line, "\n");
                // provide only packages that are [A]vailable or [I]nstalled as a result
                // (Eliminate [?] and other non-installable lines of text)
                if ((strncmp(line, "[A]", 3) == 0) || (strncmp(line, "[I]", 3) == 0))
                {
                        auto parts = Utils::String::splitAny(line, " \t");
                        if (parts.size() < 2)
                                continue;

                        ThreeFiftyOnePackage bt;
                        bt.isInstalled = (Utils::String::startsWith(parts[0], "[I]"));
                        bt.name = parts[1];
                        bt.url = parts.size() < 3 ? "" : (parts[2] == "-" ? parts[3] : parts[2]);

                        res.push_back(bt);
                }
        }
        pclose(pipe);

        getThreeFiftyOnePackagesImages(res);
        return res;
}

std::pair<std::string, int> ApiSystem::installThreeFiftyOnePackage(std::string thname, const std::function<void(const std::string)>& func)
{
        return executeScriptLegacy("351elec-es-packages install " + thname, func);
}

std::pair<std::string, int> ApiSystem::uninstallThreeFiftyOnePackage(std::string thname, const std::function<void(const std::string)>& func)
{
        return executeScriptLegacy("351elec-es-packages remove " + thname, func);
}

std::vector<BatoceraTheme> ApiSystem::getBatoceraThemesList()
{
	LOG(LogDebug) << "ApiSystem::getBatoceraThemesList";

	std::vector<BatoceraTheme> res;

	std::string command = "batocera-es-theme list";
	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
		return res;

	char line[1024];
	char *pch;

	while (fgets(line, 1024, pipe)) 
	{
		strtok(line, "\n");
		// provide only themes that are [A]vailable or [I]nstalled as a result
		// (Eliminate [?] and other non-installable lines of text)
		if ((strncmp(line, "[A]", 3) == 0) || (strncmp(line, "[I]", 3) == 0))
		{
			auto parts = Utils::String::splitAny(line, " \t");
			if (parts.size() < 2)
				continue;

			BatoceraTheme bt;
			bt.isInstalled = (Utils::String::startsWith(parts[0], "[I]"));
			bt.name = parts[1];
			bt.url = parts.size() < 3 ? "" : (parts[2] == "-" ? parts[3] : parts[2]);

			res.push_back(bt);
		}
	}
	pclose(pipe);

	getBatoceraThemesImages(res);
	return res;
}

std::string ApiSystem::getUpdateUrl()
{
	auto systemsetting = SystemConf::getInstance()->get("global.updates.url");

	std::ifstream fh("/storage/.config/.OS_ARCH");
	std::string s;
	std::string MyArch;
	if(fh) {
		std::ostringstream ss;
		ss << fh.rdbuf();
		s = ss.str();
		if (s == "RG351P") {
			MyArch = "RG351P";
		} else {
			MyArch = "RG351V";
		}
	}

	std::regex newline("\n$");
	MyArch = std::regex_replace(MyArch, newline, "");

	if (!systemsetting.empty())
		return systemsetting;
	std::string SendBack = "https://github.com/351ELEC/351ELEC/raw/main/metadata/" + MyArch;
	return SendBack;
}

void ApiSystem::getBatoceraThemesImages(std::vector<BatoceraTheme>& items)
{
	for (auto it = items.begin(); it != items.end(); ++it)
		it->image = getUpdateUrl() + "/themes/" + it->name + ".jpg";
}

std::pair<std::string, int> ApiSystem::installBatoceraTheme(std::string thname, const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-es-theme install " + thname, func);
}

std::pair<std::string, int> ApiSystem::uninstallBatoceraTheme(std::string thname, const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-es-theme remove " + thname, func);
}

std::vector<BatoceraBezel> ApiSystem::getBatoceraBezelsList()
{
	LOG(LogInfo) << "ApiSystem::getBatoceraBezelsList";

	std::vector<BatoceraBezel> res;

	auto lines = executeEnumerationScript("batocera-es-thebezelproject list");
	for (auto line : lines)
	{
		auto parts = Utils::String::splitAny(line, " \t");
		if (parts.size() < 2)
			continue;

		if (!Utils::String::startsWith(parts[0], "[I]") && !Utils::String::startsWith(parts[0], "[A]"))
			continue;

		BatoceraBezel bz;
		bz.isInstalled = (Utils::String::startsWith(parts[0], "[I]"));
		bz.name = parts[1];
		bz.url = parts.size() < 3 ? "" : (parts[2] == "-" ? parts[3] : parts[2]);
		bz.folderPath = parts.size() < 4 ? "" : parts[3];

		if (bz.name != "?")
			res.push_back(bz);
	}

	return res;
}

std::pair<std::string, int> ApiSystem::installBatoceraBezel(std::string bezelsystem, const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-es-thebezelproject install " + bezelsystem, func);
}

std::pair<std::string, int> ApiSystem::uninstallBatoceraBezel(std::string bezelsystem, const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-es-thebezelproject remove " + bezelsystem, func);
}

std::string ApiSystem::getMD5(const std::string fileName, bool fromZipContents)
{
	LOG(LogDebug) << "getMD5 >> " << fileName;

	// 7za x -so test.7z | md5sum
	std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(fileName));
	if (ext == ".zip" && fromZipContents)
	{
		Utils::Zip::ZipFile file;
		if (file.load(fileName))
		{
			std::string romName;

			for (auto name : file.namelist())
			{
				if (Utils::FileSystem::getExtension(name) != ".txt" && !Utils::String::endsWith(name, "/"))
				{
					if (!romName.empty())
					{
						romName = "";
						break;
					}

					romName = name;
				}
			}

			if (!romName.empty())
				return file.getFileMd5(romName);
		}
	}

#if !WIN32
	if (fromZipContents && ext == ".7z")
	{
		auto cmd = getSevenZipCommand() + " x -so \"" + fileName + "\" | md5sum";
		auto ret = executeEnumerationScript(cmd);
		if (ret.size() == 1 && ret.cbegin()->length() >= 32)
			return ret.cbegin()->substr(0, 32);
	}
#endif

	std::string contentFile = fileName;
	std::string ret;
	std::string tmpZipDirectory;

	if (fromZipContents && ext == ".7z")
	{
		tmpZipDirectory = Utils::FileSystem::combine(Utils::FileSystem::getTempPath(), Utils::FileSystem::getStem(fileName));
		Utils::FileSystem::deleteDirectoryFiles(tmpZipDirectory);

		if (unzipFile(fileName, tmpZipDirectory))
		{
			auto fileList = Utils::FileSystem::getDirContent(tmpZipDirectory, true);

			std::vector<std::string> res;
			std::copy_if(fileList.cbegin(), fileList.cend(), std::back_inserter(res), [](const std::string file) { return Utils::FileSystem::getExtension(file) != ".txt";  });
		
			if (res.size() == 1)
				contentFile = *res.cbegin();
		}

		// if there's no file or many files ? get md5 of archive
	}

	ret = Utils::FileSystem::getFileMd5(contentFile);

	if (!tmpZipDirectory.empty())
		Utils::FileSystem::deleteDirectoryFiles(tmpZipDirectory, true);

	LOG(LogDebug) << "getMD5 << " << ret;

	return ret;
}

std::string ApiSystem::getCRC32(std::string fileName, bool fromZipContents)
{
	LOG(LogDebug) << "getCRC32 >> " << fileName;

	std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(fileName));

	if (ext == ".7z" && fromZipContents)
	{
		LOG(LogDebug) << "getCRC32 is using 7z";

		std::string fn = Utils::FileSystem::getFileName(fileName);
		auto cmd = getSevenZipCommand() + " l -slt \"" + fileName + "\"";
		auto lines = executeEnumerationScript(cmd);
		for (std::string all : lines)
		{
			int idx = all.find("CRC = ");
			if (idx != std::string::npos)
				return all.substr(idx + 6);
			else if (all.find(fn) == (all.size() - fn.size()) && all.length() > 8 && all[9] == ' ')
				return all.substr(0, 8);
		}
	}
	else if (ext == ".zip" && fromZipContents)
	{
		LOG(LogDebug) << "getCRC32 is using ZipFile";

		Utils::Zip::ZipFile file;
		if (file.load(fileName))
		{
			std::string romName;

			for (auto name : file.namelist())
			{
				if (Utils::FileSystem::getExtension(name) != ".txt" && !Utils::String::endsWith(name, "/"))
				{
					if (!romName.empty())
					{
						romName = "";
						break;
					}

					romName = name;
				}
			}

			if (!romName.empty())
				return file.getFileCrc(romName);
		}
	}

	LOG(LogDebug) << "getCRC32 is using fileBuffer";
	return Utils::FileSystem::getFileCrc32(fileName);
}

bool ApiSystem::unzipFile(const std::string fileName, const std::string destFolder, const std::function<bool(const std::string)>& shouldExtract)
{
	LOG(LogDebug) << "unzipFile >> " << fileName << " to " << destFolder;

	if (!Utils::FileSystem::exists(destFolder))
		Utils::FileSystem::createDirectory(destFolder);
		
	if (Utils::String::toLower(Utils::FileSystem::getExtension(fileName)) == ".zip")
	{
		LOG(LogDebug) << "unzipFile is using ZipFile";

		Utils::Zip::ZipFile file;
		if (file.load(fileName))
		{
			for (auto name : file.namelist())
			{
				if (Utils::String::endsWith(name, "/"))
				{
					Utils::FileSystem::createDirectory(Utils::FileSystem::combine(destFolder, name.substr(0, name.length() - 1)));
					continue;
				}

				if (shouldExtract != nullptr && !shouldExtract(Utils::FileSystem::combine(destFolder, name)))
					continue;

				file.extract(name, destFolder);
			}

			LOG(LogDebug) << "unzipFile << OK";
			return true;
		}

		LOG(LogDebug) << "unzipFile << KO Bad format ?" << fileName;
		return false;
	}
	
	LOG(LogDebug) << "unzipFile is using 7z";

	std::string cmd = getSevenZipCommand() + " x \"" + Utils::FileSystem::getPreferredPath(fileName) + "\" -y -o\"" + Utils::FileSystem::getPreferredPath(destFolder) + "\"";
	bool ret = executeScriptLegacy(cmd);
	LOG(LogDebug) << "unzipFile <<";
	return ret;
}

std::vector<std::string> ApiSystem::getWifiNetworks(bool scan)
{
	executeScriptLegacy("/usr/bin/wifictl scan");
	return executeEnumerationScript("/usr/bin/wifictl list");
}

std::vector<std::string> ApiSystem::executeEnumerationScript(const std::string command)
{
	LOG(LogDebug) << "ApiSystem::executeEnumerationScript -> " << command;

	std::vector<std::string> res;

	FILE *pipe = popen(command.c_str(), "r");

	if (pipe == NULL)
		return res;

	char line[1024];
	while (fgets(line, 1024, pipe))
	{
		strtok(line, "\n");
		res.push_back(std::string(line));
	}

	pclose(pipe);
	return res;
}

std::vector<std::string> ApiSystem::executeScript(const std::string& command) {
  std::vector<std::string> vec;
  executeScript(command, [&vec](const std::string& line) {
    vec.push_back(line);
  });
  return vec;
}

std::pair<std::string, int> ApiSystem::executeScript(const std::string& command, const std::function<void(const std::string)>& func)
{  
  std::cout << "ApiSystem::executeScript -> " << command << std::endl;
	LOG(LogInfo) << "ApiSystem::executeScript -> " << command;

	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
	{
		LOG(LogError) << "Error executing " << command;
		return std::pair<std::string, int>("Error starting command : " + command, -1);
	}

  std::stringstream output_stream;
	char buff[1024];
	while (fgets(buff, 1024, pipe))
	{
    std::stringstream output_stream(buff);
    std::string line;
    while (getline(output_stream, line).good()) {
      if (line.empty()) continue;
      std::cout << line << std::endl;
      if (func != nullptr) {
        func(std::string(line));
      }
    }
	}

	int exitCode = WEXITSTATUS(pclose(pipe));
	return std::pair<std::string, int>("", exitCode);
}

std::pair<std::string, int> ApiSystem::executeScriptLegacy(const std::string command, const std::function<void(const std::string)>& func)
{
	LOG(LogInfo) << "ApiSystem::executeScriptLegacy -> " << command;

	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
	{
		LOG(LogError) << "Error executing " << command;
		return std::pair<std::string, int>("Error starting command : " + command, -1);
	}

	char line[1024];
	while (fgets(line, 1024, pipe))
	{
		strtok(line, "\n");

		// Long theme names/URL can crash the GUI MsgBox
		// "48" found by trials and errors. Ideally should be fixed
		// in es-core MsgBox -- FIXME
		if (strlen(line) > 48)
			line[47] = '\0';

		if (func != nullptr)
			func(std::string(line));
	}

	int exitCode = WEXITSTATUS(pclose(pipe));
	return std::pair<std::string, int>(line, exitCode);
}

bool ApiSystem::executeScriptLegacy(const std::string command)
{	
	LOG(LogInfo) << "Running " << command;

	if (system(command.c_str()) == 0)
		return true;
	
	LOG(LogError) << "Error executing " << command;
	return false;
}

bool ApiSystem::isScriptingSupported(ScriptId script)
{
	std::vector<std::string> executables;

	switch (script)
	{
	case ApiSystem::RETROACHIVEMENTS:
#ifdef CHEEVOS_DEV_LOGIN
		return true;
#endif
		break;
	case ApiSystem::KODI:
		executables.push_back("kodi");
		break;
	case ApiSystem::WIFI:
		executables.push_back("wifictl");
		break;
	case ApiSystem::BLUETOOTH:
		executables.push_back("batocera-bluetooth");
		break;
	case ApiSystem::RESOLUTION:
		executables.push_back("batocera-resolution");
		break;
	case ApiSystem::BIOSINFORMATION:
		executables.push_back("batocera-systems");
		break;
	case ApiSystem::DISKFORMAT:
		executables.push_back("batocera-format");
		break;
	case ApiSystem::OVERCLOCK:
		executables.push_back("batocera-overclock");
		break;
	case ApiSystem::THEMESDOWNLOADER:
		executables.push_back("batocera-es-theme");
		break;		
	case ApiSystem::NETPLAY:
		executables.push_back("7zr");
		break;
	case ApiSystem::PDFEXTRACTION:
		executables.push_back("pdftoppm");
		executables.push_back("pdfinfo");
		break;
	case ApiSystem::BATOCERASTORE:
		executables.push_back("batocera-store");
		break;
	case ApiSystem::THEBEZELPROJECT:
		executables.push_back("batocera-es-thebezelproject");
		break;		
	case ApiSystem::PADSINFO:
		executables.push_back("batocera-padsinfo");
		break;
	case ApiSystem::EVMAPY:
		executables.push_back("evmapy");
		break;
	case ApiSystem::BATOCERAPREGAMELISTSHOOK:
		executables.push_back("batocera-preupdate-gamelists-hook");
		break;
	}

	if (executables.size() == 0)
		return true;

	for (auto executable : executables)
		if (!Utils::FileSystem::exists("/usr/bin/" + executable))
			return false;

	return true;
}

bool ApiSystem::downloadFile(const std::string url, const std::string fileName, const std::string label, const std::function<void(const std::string)>& func)
{
	if (func != nullptr)
		func("Downloading " + label);

	HttpReq httpreq(url, fileName);
	while (httpreq.status() == HttpReq::REQ_IN_PROGRESS)
	{
		if (func != nullptr)
			func(std::string("Downloading " + label + " >>> " + std::to_string(httpreq.getPercent()) + " %"));

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	if (httpreq.status() != HttpReq::REQ_SUCCESS)
		return false;

	return true;
}

void ApiSystem::setReadyFlag(bool ready)
{
	if (!ready)
	{
		Utils::FileSystem::removeFile("/tmp/emulationstation.ready");
		return;
	}

	FILE* fd = fopen("/tmp/emulationstation.ready", "w");
	if (fd != NULL) 
		fclose(fd);
}

bool ApiSystem::isReadyFlagSet()
{
	return Utils::FileSystem::exists("/tmp/emulationstation.ready");
}

std::vector<std::string> ApiSystem::getFormatDiskList()
{
#if WIN32 && _DEBUG
	std::vector<std::string> ret;
	ret.push_back("d:\ DRIVE D:");
	ret.push_back("e:\ DRIVE Z:");
	return ret;
#endif
	return executeEnumerationScript("batocera-format listDisks");
}

std::vector<std::string> ApiSystem::getFormatFileSystems()
{
#if WIN32 && _DEBUG
	std::vector<std::string> ret;
	ret.push_back("exfat");	
	ret.push_back("brfs");
	return ret;
#endif
	return executeEnumerationScript("batocera-format listFstypes");
}

int ApiSystem::formatDisk(const std::string disk, const std::string format, const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-format format " + disk + " " + format, func).second;
}

int ApiSystem::getPdfPageCount(const std::string fileName)
{
	auto lines = executeEnumerationScript("pdfinfo \"" + fileName + "\"");
	for (auto line : lines)
	{
		auto splits = Utils::String::split(line, ':', true);
		if (splits.size() == 2 && splits[0] == "Pages")
			return atoi(Utils::String::trim(splits[1]).c_str());
	}

	return 0;
}

std::vector<std::string> ApiSystem::extractPdfImages(const std::string fileName, int pageIndex, int pageCount, bool bestQuality)
{
	auto pdfFolder = Utils::FileSystem::getPdfTempPath();

	std::vector<std::string> ret;

	if (pageIndex < 0)
	{
		Utils::FileSystem::deleteDirectoryFiles(pdfFolder);

		int hardWareCoreCount = std::thread::hardware_concurrency();
		if (hardWareCoreCount > 1)
		{
			int lastTime = SDL_GetTicks();

			int numberOfPagesToProcess = 1;
			if (hardWareCoreCount < 8)
				numberOfPagesToProcess = 2;

			int pc = getPdfPageCount(fileName);
			if (pc > 0)
			{
				Utils::ThreadPool pool(1);

				for (int i = 0; i < pc; i += numberOfPagesToProcess)
					pool.queueWorkItem([this, fileName, i, numberOfPagesToProcess] { extractPdfImages(fileName, i + 1, numberOfPagesToProcess); });

				pool.wait();

				int time = SDL_GetTicks() - lastTime;
				std::string timeText = std::to_string(time) + "ms";

				for (auto file : Utils::FileSystem::getDirContent(pdfFolder, false))
				{
					auto ext = Utils::String::toLower(Utils::FileSystem::getExtension(file));
					if (ext != ".jpg" && ext != ".png" && ext != ".ppm")
						continue;

					ret.push_back(file);
				}

				std::sort(ret.begin(), ret.end());
			}

			return ret;
		}
	}

	int lastTime = SDL_GetTicks();

	std::string page;

	std::string quality = Renderer::isSmallScreen() ? "96" : "125";
	if (bestQuality)
		quality = "300";

	std::string prefix = "extract";
	if (pageIndex >= 0)
	{
		char buffer[12];
		sprintf(buffer, "%08d", (uint32_t)pageIndex);
		prefix = "page-" + quality + "-" + std::string(buffer) + "-pdf";

		page = " -f " + std::to_string(pageIndex) + " -l " + std::to_string(pageIndex + pageCount - 1);
	}

#if WIN32
	executeEnumerationScript("pdftoppm -r "+ quality + page +" \"" + fileName + "\" \""+ pdfFolder +"/" + prefix +"\"");
#else
	executeEnumerationScript("pdftoppm -jpeg -r "+ quality +" -cropbox" + page + " \"" + fileName + "\" \"" + pdfFolder + "/" + prefix + "\"");
#endif

	int time = SDL_GetTicks() - lastTime;
	std::string text = std::to_string(time);
	
	for (auto file : Utils::FileSystem::getDirContent(pdfFolder, false))
	{
		auto ext = Utils::String::toLower(Utils::FileSystem::getExtension(file));
		if (ext != ".jpg" && ext != ".png" && ext != ".ppm")
			continue;

		if (pageIndex >= 0 && !Utils::String::startsWith(Utils::FileSystem::getFileName(file), prefix))
			continue;

		ret.push_back(file);
	}

	std::sort(ret.begin(), ret.end());
	return ret;
}


std::vector<PacmanPackage> ApiSystem::getBatoceraStorePackages()
{
	std::vector<PacmanPackage> packages;

	LOG(LogDebug) << "ApiSystem::getBatoceraStorePackages";

	auto res = executeEnumerationScript("batocera-store list");
	std::string data = Utils::String::join(res, "\n");
	if (data.empty())
	{
		LOG(LogError) << "Package list is empty";
		return packages;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_string(data.c_str());
	if (!result)
	{
		LOG(LogError) << "Unable to parse packages";
		return packages;
	}

	pugi::xml_node root = doc.child("packages");
	if (!root)
	{
		LOG(LogError) << "Could not find <packages> node";
		return packages;
	}

	for (pugi::xml_node pkgNode = root.child("package"); pkgNode; pkgNode = pkgNode.next_sibling("package"))
	{
		PacmanPackage package;

		for (pugi::xml_node node = pkgNode.first_child(); node; node = node.next_sibling())
		{
			std::string tag = node.name();
			if (tag == "name")
				package.name = node.text().get();
			if (tag == "repository")
				package.repository = node.text().get();
			if (tag == "available_version")
				package.available_version = node.text().get();
			if (tag == "description")
				package.description = node.text().get();
			if (tag == "group")
				package.group = node.text().get(); // groups.push_back(
			if (tag == "license")
				package.licenses.push_back(node.text().get());
			if (tag == "packager")
				package.packager = node.text().get();
			if (tag == "status")
				package.status = node.text().get();
			if (tag == "repository")
				package.repository = node.text().get();
			if (tag == "url")
				package.url = node.text().get();			
			if (tag == "arch")
				package.arch = node.text().get();
			if (tag == "download_size")
				package.download_size = node.text().as_llong();
			if (tag == "installed_size")
				package.installed_size = node.text().as_llong();
		}

		if (!package.name.empty())
			packages.push_back(package);		
	}

	return packages;
}

std::pair<std::string, int> ApiSystem::installBatoceraStorePackage(std::string name, const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-store install \"" + name + "\"", func);
}

std::pair<std::string, int> ApiSystem::uninstallBatoceraStorePackage(std::string name, const std::function<void(const std::string)>& func)
{
	return executeScriptLegacy("batocera-store remove \"" + name + "\"", func);
}

void ApiSystem::refreshBatoceraStorePackageList()
{
	executeScriptLegacy("batocera-store refresh");
	executeScriptLegacy("batocera-store clean-all");
}

void ApiSystem::callBatoceraPreGameListsHook()
{
	executeScriptLegacy("batocera-preupdate-gamelists-hook");
}

void ApiSystem::updateBatoceraStorePackageList()
{
	executeScriptLegacy("batocera-store update");
}

std::vector<std::string> ApiSystem::getShaderList(const std::string systemName)
{
	Utils::FileSystem::FileSystemCacheActivator fsc;

	std::vector<std::string> ret;

	std::vector<std::string> folderList = { "/usr/share/batocera/shaders/configs", "/storage/shaders/configs" };
	for (auto folder : folderList)
	{
		for (auto file : Utils::FileSystem::getDirContent(folder, true))
		{
			if (Utils::FileSystem::getFileName(file) == "rendering-defaults.yml")
			{
				auto parent = Utils::FileSystem::getFileName(Utils::FileSystem::getParent(file));
				if (parent == "configs")
					continue;

				if (std::find(ret.cbegin(), ret.cend(), parent) == ret.cend())
					ret.push_back(parent);
			}
		}
	}

	std::sort(ret.begin(), ret.end());
	return ret;
}


std::vector<std::string> ApiSystem::getRetroachievementsSoundsList()
{
	Utils::FileSystem::FileSystemCacheActivator fsc;

	std::vector<std::string> ret;

	LOG(LogDebug) << "ApiSystem::getRetroAchievementsSoundsList";

	std::vector<std::string> folderList = { "/usr/share/libretro/assets/sounds", "/storage/sounds/retroachievements" };
	for (auto folder : folderList)
	{
		for (auto file : Utils::FileSystem::getDirContent(folder, false))
		{
			auto sound = Utils::FileSystem::getFileName(file);
			if (sound.substr(sound.find_last_of('.') + 1) == "ogg")
			{
				if (std::find(ret.cbegin(), ret.cend(), sound) == ret.cend())
				  ret.push_back(sound.substr(0, sound.find_last_of('.')));
			}
		}
	}

	std::sort(ret.begin(), ret.end());
	return ret;
}

std::vector<std::string> ApiSystem::getTimezones()
{
	std::vector<std::string> ret;

	LOG(LogDebug) << "ApiSystem::getTimezones";

	std::vector<std::string> folderList = { "/usr/share/zoneinfo/" };
	for (auto folder : folderList)
	{
		for (auto continent : Utils::FileSystem::getDirContent(folder, false))
		{
			for (auto file : Utils::FileSystem::getDirContent(continent, false))
			{
				std::string short_continent = continent.substr(continent.find_last_of('/') + 1, -1);
				if (short_continent == "Africa" || short_continent == "America"
					|| short_continent == "Antarctica" || short_continent == "Asia"
					|| short_continent == "Atlantic" || short_continent == "Australia"
					|| short_continent == "Etc" || short_continent == "Europe"
					|| short_continent == "Indian" || short_continent == "Pacific")
				{
					auto tz = Utils::FileSystem::getFileName(file);
					if (std::find(ret.cbegin(), ret.cend(), tz) == ret.cend())
						  ret.push_back(short_continent + "/" + tz);
				}
			}
		}
	}
	std::sort(ret.begin(), ret.end());
	return ret;
}

std::string ApiSystem::getCurrentTimezone()
{
	LOG(LogInfo) << "ApiSystem::getCurrentTimezone";
	auto cmd = executeEnumerationScript("batocera-timezone get");
	std::string tz = Utils::String::join(cmd, "");
	remove_if(tz.begin(), tz.end(), isspace);
	if (tz.empty()) {
		cmd = executeEnumerationScript("batocera-timezone detect");
		tz = Utils::String::join(cmd, "");
	}
	return tz;
}

bool ApiSystem::setTimezone(std::string tz)
{
	if (tz.empty())
		return false;
	return executeScriptLegacy("batocera-timezone set \"" + tz + "\"");
}

std::vector<PadInfo> ApiSystem::getPadsInfo()
{
	LOG(LogInfo) << "ApiSystem::getPadsInfo";

	std::vector<PadInfo> ret;

	auto res = executeEnumerationScript("batocera-padsinfo");
	std::string data = Utils::String::join(res, "\n");
	if (data.empty())
	{
		LOG(LogError) << "Package list is empty";
		return ret;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_string(data.c_str());
	if (!result)
	{
		LOG(LogError) << "Unable to parse packages";
		return ret;
	}

	pugi::xml_node root = doc.child("pads");
	if (!root)
	{
		LOG(LogError) << "Could not find <pads> node";
		return ret;
	}

	for (pugi::xml_node pad = root.child("pad"); pad; pad = pad.next_sibling("pad"))
	{
		PadInfo pi;

		if (pad.attribute("device"))
			pi.device = pad.attribute("device").as_string();

		if (pad.attribute("id"))
			pi.id = Utils::String::toInteger(pad.attribute("id").as_string());

		if (pad.attribute("name"))
			pi.name = pad.attribute("name").as_string();

		if (pad.attribute("battery"))
			pi.battery = Utils::String::toInteger(pad.attribute("battery").as_string());

		if (pad.attribute("status"))
			pi.status = pad.attribute("status").as_string();

		ret.push_back(pi);
	}

	return ret;
}

std::string ApiSystem::getRunningArchitecture()
{
	auto res = executeEnumerationScript("uname -m");
	if (res.size() > 0)
		return res[0];

	return "";
}

std::string ApiSystem::getHostsName()
{
	auto hostName = SystemConf::getInstance()->get("system.hostname");
	if (!hostName.empty())
		return hostName;

	return "127.0.0.1";
}

bool ApiSystem::emuKill()
{
	LOG(LogDebug) << "ApiSystem::emuKill";

	return executeScriptLegacy("batocera-es-swissknife --emukill");
}
