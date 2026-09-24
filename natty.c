#include <windows.h>
#include <winhttp.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// ============================================================
// SENSOR STRUCTURE
// ============================================================

struct Sensor
{
    std::string name;
    std::string type;
    std::string value;
    std::string id;
};

// ============================================================
// HTTP GET FROM LIBRE HARDWARE MONITOR
// ============================================================

std::string httpGet()
{
    HINTERNET session = WinHttpOpen(
        L"DevMonitor/2.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session)
        return "";

    HINTERNET connect = WinHttpConnect(
        session,
        L"127.0.0.1",
        8085,
        0
    );

    if (!connect)
    {
        WinHttpCloseHandle(session);
        return "";
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        L"/data.json",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );

    if (!request)
    {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return "";
    }

    BOOL ok = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        nullptr,
        0,
        0,
        0
    );

    if (ok)
    {
        ok = WinHttpReceiveResponse(
            request,
            nullptr
        );
    }

    std::string result;

    if (ok)
    {
        DWORD available = 0;

        while (
            WinHttpQueryDataAvailable(
                request,
                &available
            ) &&
            available > 0
        )
        {
            std::vector<char> buffer(
                available + 1,
                '\0'
            );

            DWORD downloaded = 0;

            if (!WinHttpReadData(
                    request,
                    buffer.data(),
                    available,
                    &downloaded))
            {
                break;
            }

            result.append(
                buffer.data(),
                downloaded
            );
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    return result;
}

// ============================================================
// RECURSIVELY FIND ALL SENSORS
// ============================================================

void findSensors(
    const json& node,
    std::vector<Sensor>& sensors
)
{
    if (!node.is_object())
        return;

    if (
        node.contains("SensorId") &&
        node.contains("Type")
    )
    {
        Sensor sensor;

        sensor.name =
            node.value("Text", "");

        sensor.type =
            node.value("Type", "");

        sensor.value =
            node.value("Value", "");

        sensor.id =
            node.value("SensorId", "");

        sensors.push_back(sensor);
    }

    if (
        node.contains("Children") &&
        node["Children"].is_array()
    )
    {
        for (
            const auto& child :
            node["Children"]
        )
        {
            findSensors(
                child,
                sensors
            );
        }
    }
}

// ============================================================
// LOWERCASE STRING
// ============================================================

std::string lower(
    std::string text
)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c)
            );
        }
    );

    return text;
}

// ============================================================
// FIND SENSOR
// ============================================================

std::string findSensor(
    const std::vector<Sensor>& sensors,
    const std::vector<std::string>& names,
    const std::string& type
)
{
    // First pass:
    // Look for exact/strong matches.

    for (const auto& sensor : sensors)
    {
        if (
            lower(sensor.type) !=
            lower(type)
        )
        {
            continue;
        }

        std::string sensorName =
            lower(sensor.name);

        for (const auto& wanted : names)
        {
            std::string wantedName =
                lower(wanted);

            if (
                sensorName.find(wantedName)
                != std::string::npos
            )
            {
                return sensor.value;
            }
        }
    }

    return "--";
}

// ============================================================
// EXTRACT NUMBER FROM SENSOR VALUE
// ============================================================

double numberFrom(
    const std::string& text
)
{
    std::string number;

    bool started = false;

    for (char c : text)
    {
        if (
            (c >= '0' && c <= '9') ||
            c == '.' ||
            c == '-'
        )
        {
            number += c;
            started = true;
        }
        else if (started)
        {
            break;
        }
    }

    if (number.empty())
        return -1;

    try
    {
        return std::stod(number);
    }
    catch (...)
    {
        return -1;
    }
}

// ============================================================
// CREATE ASCII BAR
// ============================================================

std::string bar(
    double value,
    int width = 10
)
{
    if (value < 0)
        value = 0;

    if (value > 100)
        value = 100;

    int filled =
        static_cast<int>(
            (value / 100.0) *
            width
        );

    std::string result;

    for (int i = 0; i < width; i++)
    {
        if (i < filled)
            result += "#";
        else
            result += "-";
    }

    return result;
}

// ============================================================
// CPU HISTORY
// ============================================================

std::vector<double> cpuHistory;

// ============================================================
// PRINT CPU HISTORY
// ============================================================

void printGraph()
{
    if (cpuHistory.empty())
    {
        std::cout
            << "No data yet";
        return;
    }

    for (double value : cpuHistory)
    {
        if (value < 10)
            std::cout << "_";
        else if (value < 20)
            std::cout << ".";
        else if (value < 30)
            std::cout << ":";
        else if (value < 40)
            std::cout << "-";
        else if (value < 50)
            std::cout << "=";
        else if (value < 60)
            std::cout << "+";
        else if (value < 70)
            std::cout << "*";
        else if (value < 80)
            std::cout << "#";
        else if (value < 90)
            std::cout << "%";
        else
            std::cout << "@";
    }
}

// ============================================================
// CLEAR SCREEN
// ============================================================

void clearScreen()
{
    system("cls");
}

// ============================================================
// GET RAM INFORMATION
// ============================================================

void getRAM(
    double& usedGB,
    double& totalGB,
    double& percent
)
{
    MEMORYSTATUSEX memory;

    memory.dwLength =
        sizeof(MEMORYSTATUSEX);

    if (
        GlobalMemoryStatusEx(
            &memory
        )
    )
    {
        totalGB =
            static_cast<double>(
                memory.ullTotalPhys
            ) /
            (1024.0 * 1024.0 * 1024.0);

        double availableGB =
            static_cast<double>(
                memory.ullAvailPhys
            ) /
            (1024.0 * 1024.0 * 1024.0);

        usedGB =
            totalGB -
            availableGB;

        percent =
            static_cast<double>(
                memory.dwMemoryLoad
            );
    }
    else
    {
        usedGB = 0;
        totalGB = 0;
        percent = 0;
    }
}

// ============================================================
// GET STORAGE INFORMATION
// ============================================================

void getStorage(
    double& usedGB,
    double& totalGB,
    double& percent
)
{
    ULARGE_INTEGER freeBytes;
    ULARGE_INTEGER totalBytes;
    ULARGE_INTEGER totalFreeBytes;

    BOOL result =
        GetDiskFreeSpaceExA(
            "C:\\",
            &freeBytes,
            &totalBytes,
            &totalFreeBytes
        );

    if (result)
    {
        totalGB =
            static_cast<double>(
                totalBytes.QuadPart
            ) /
            (1024.0 * 1024.0 * 1024.0);

        double freeGB =
            static_cast<double>(
                totalFreeBytes.QuadPart
            ) /
            (1024.0 * 1024.0 * 1024.0);

        usedGB =
            totalGB -
            freeGB;

        if (totalGB > 0)
        {
            percent =
                (usedGB / totalGB) *
                100.0;
        }
        else
        {
            percent = 0;
        }
    }
    else
    {
        usedGB = 0;
        totalGB = 0;
        percent = 0;
    }
}

// ============================================================
// PRINT HEADER
// ============================================================

void printHeader()
{
    std::cout
        << "+------------------------------------------+\n"
        << "|              DEVMONITOR v2               |\n"
        << "+------------------------------------------+\n";
}

// ============================================================
// PRINT CPU
// ============================================================

void printCPU(
    double cpu
)
{
    std::cout
        << "| CPU   ["
        << bar(cpu)
        << "] ";

    if (cpu >= 0)
    {
        std::cout
            << std::fixed
            << std::setprecision(0)
            << std::setw(3)
            << cpu
            << "%";
    }
    else
    {
        std::cout
            << " --";
    }

    std::cout
        << "                   |\n";
}

// ============================================================
// PRINT CPU TEMP
// ============================================================

void printCPUTemp(
    const std::string& temp
)
{
    std::cout
        << "| TEMP  "
        << std::setw(8)
        << std::left
        << temp
        << std::right
        << "                    |\n";
}

// ============================================================
// PRINT GPU
// ============================================================

void printGPU(
    double gpu
)
{
    std::cout
        << "| GPU   ["
        << bar(gpu)
        << "] ";

    if (gpu >= 0)
    {
        std::cout
            << std::fixed
            << std::setprecision(0)
            << std::setw(3)
            << gpu
            << "%";
    }
    else
    {
        std::cout
            << " --";
    }

    std::cout
        << "                   |\n";
}

// ============================================================
// PRINT GPU TEMP
// ============================================================

void printGPUTemp(
    const std::string& temp
)
{
    std::cout
        << "| TEMP  "
        << std::setw(8)
        << std::left
        << temp
        << std::right
        << "                    |\n";
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    // Fix Windows console behavior
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    while (true)
    {
        // ----------------------------------------------------
        // GET DATA FROM LIBRE HARDWARE MONITOR
        // ----------------------------------------------------

        std::string data =
            httpGet();

        if (data.empty())
        {
            clearScreen();

            printHeader();

            std::cout
                << "|\n"
                << "| ERROR: Cannot connect to LHM.           |\n"
                << "|                                          |\n"
                << "| Make sure Libre Hardware Monitor is     |\n"
                << "| running with Remote Web Server enabled. |\n"
                << "|                                          |\n"
                << "+------------------------------------------+\n";

            std::this_thread::sleep_for(
                std::chrono::seconds(2)
            );

            continue;
        }

        try
        {
            // ------------------------------------------------
            // PARSE JSON
            // ------------------------------------------------

            json root =
                json::parse(data);

            // ------------------------------------------------
            // FIND SENSORS
            // ------------------------------------------------

            std::vector<Sensor> sensors;

            findSensors(
                root,
                sensors
            );

            // ------------------------------------------------
            // CPU USAGE
            // ------------------------------------------------

            std::string cpuUsage =
                findSensor(
                    sensors,
                    {
                        "CPU Total",
                        "CPU Core",
                        "CPU Package"
                    },
                    "Load"
                );

            // ------------------------------------------------
            // CPU TEMPERATURE
            // ------------------------------------------------

            std::string cpuTemp =
                findSensor(
                    sensors,
                    {
                        "CPU Package",
                        "Core Average",
                        "CPU Core",
                        "Core (Tctl/Tdie)",
                        "Package"
                    },
                    "Temperature"
                );

            // ------------------------------------------------
            // GPU USAGE
            // ------------------------------------------------

            std::string gpuUsage =
                findSensor(
                    sensors,
                    {
                        "GPU Core",
                        "GPU Total",
                        "GPU"
                    },
                    "Load"
                );

            // ------------------------------------------------
            // GPU TEMPERATURE
            // ------------------------------------------------

            std::string gpuTemp =
                findSensor(
                    sensors,
                    {
                        "GPU Core",
                        "GPU Temperature",
                        "GPU Hot Spot",
                        "GPU"
                    },
                    "Temperature"
                );

            // ------------------------------------------------
            // CONVERT CPU/GPU VALUES
            // ------------------------------------------------

            double cpu =
                numberFrom(
                    cpuUsage
                );

            double gpu =
                numberFrom(
                    gpuUsage
                );

            // ------------------------------------------------
            // CPU HISTORY
            // ------------------------------------------------

            if (cpu >= 0)
            {
                cpuHistory.push_back(
                    cpu
                );

                if (
                    cpuHistory.size()
                    > 40
                )
                {
                    cpuHistory.erase(
                        cpuHistory.begin()
                    );
                }
            }

            // ------------------------------------------------
            // RAM
            // ------------------------------------------------

            double ramUsed = 0;
            double ramTotal = 0;
            double ramPercent = 0;

            getRAM(
                ramUsed,
                ramTotal,
                ramPercent
            );

            // ------------------------------------------------
            // STORAGE
            // ------------------------------------------------

            double storageUsed = 0;
            double storageTotal = 0;
            double storagePercent = 0;

            getStorage(
                storageUsed,
                storageTotal,
                storagePercent
            );

            // ------------------------------------------------
            // DRAW UI
            // ------------------------------------------------

            clearScreen();

            printHeader();

            std::cout
                << "|\n";

            // CPU
            printCPU(
                cpu
            );

            // CPU temperature
            printCPUTemp(
                cpuTemp
            );

            std::cout
                << "|\n";

            // RAM
            std::cout
                << "| RAM   ["
                << bar(
                    ramPercent
                )
                << "] ";

            std::cout
                << std::fixed
                << std::setprecision(1)
                << ramUsed
                << " / "
                << ramTotal
                << " GB";

            std::cout
                << "       |\n";

            std::cout
                << "|\n";

            // GPU
            printGPU(
                gpu
            );

            // GPU temperature
            printGPUTemp(
                gpuTemp
            );

            std::cout
                << "|\n";

            // STORAGE
            std::cout
                << "| DISK  ["
                << bar(
                    storagePercent
                )
                << "] ";

            std::cout
                << std::fixed
                << std::setprecision(1)
                << storageUsed
                << " / "
                << storageTotal
                << " GB";

            std::cout
                << "       |\n";

            std::cout
                << "|\n";

            // HISTORY
            std::cout
                << "| CPU HISTORY                             |\n"
                << "| ";

            printGraph();

            std::cout
                << "\n"
                << "|\n";

            // Footer
            std::cout
                << "+------------------------------------------+\n"
                << "| Updating every 1 second                  |\n"
                << "| Libre Hardware Monitor -> DevMonitor     |\n"
                << "+------------------------------------------+\n";

        }
        catch (
            const std::exception& e
        )
        {
            clearScreen();

            printHeader();

            std::cout
                << "|\n"
                << "| JSON ERROR                               |\n"
                << "|\n"
                << "| "
                << e.what()
                << "\n"
                << "|\n"
                << "+------------------------------------------+\n";
        }

        // ----------------------------------------------------
        // UPDATE EVERY SECOND
        // ----------------------------------------------------

        std::this_thread::sleep_for(
            std::chrono::seconds(1)
        );
    }

    return 0;
}
