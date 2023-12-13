#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>
#include <unordered_set>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <io.h>
#include <fcntl.h>

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;

class BluetoothLEManager {
public:
    BluetoothLEManager() {
        watcher.ScanningMode(BluetoothLEScanningMode::Active);
    }

    void StartScanning() {
        StartWatcher();
    }

    void StopScanning() {
        StopWatcher();
    }

    void ConnectToDevice(int index) {
        if (indexedDevices.count(index)) {
            auto connectedDevice = Connect(indexedDevices.at(index));
            if (connectedDevice) {
                SubscribeToTemperatureAndHumidityMeasurement(connectedDevice);
            }
            else {
                std::wcout << L"Failed to connect to the device." << std::endl;
            }
        }
        else {
            std::wcout << L"Invalid index selected." << std::endl;
        }
    }

    void StopSubscription() {
        continueRunning.store(false);
    }

private:
    //std::function<void(GattCharacteristic, GattValueChangedEventArgs)> onHeartRateMeasurementReceived;

    BluetoothLEAdvertisementWatcher watcher;
    std::unordered_set<uint64_t> uniqueDevices;
    std::map<int, uint64_t> indexedDevices;
    int deviceIndex = 1;
    std::atomic<bool> continueRunning{ true };

    void StartWatcher() {
        watcher.Received([&](const auto&, const BluetoothLEAdvertisementReceivedEventArgs& args) {
            HandleAdvertisement(args);
            });

        watcher.Start();
        std::wcout << L"Scanning for devices. Press Enter to stop scanning." << std::endl;
    }

    void StopWatcher() {
        std::wstring input;
        std::getline(std::wcin, input);
        watcher.Stop();
    }

    void HandleAdvertisement(const BluetoothLEAdvertisementReceivedEventArgs& args) {
        auto deviceAddress = args.BluetoothAddress();
        if (uniqueDevices.insert(deviceAddress).second) {
            indexedDevices[deviceIndex] = deviceAddress;
            auto localName = args.Advertisement().LocalName();
            localName = localName.empty() ? L"Unknown" : localName;
            std::wcout << L"[" << deviceIndex << L"] Device found: " << localName.c_str() << L" (" << deviceAddress << L")" << std::endl;
            deviceIndex++;
        }
    }

    BluetoothLEDevice Connect(uint64_t bluetoothAddress) {
        BluetoothLEDevice bleDevice = nullptr;
        try {
            auto bleDeviceOperation = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress).get();
            if (bleDeviceOperation) {
                std::wcout << L"Connected to device: " << bleDeviceOperation.DeviceId().c_str() << std::endl;
                bleDevice = bleDeviceOperation;
            }
            else {
                std::wcout << L"Failed to connect to device." << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        return bleDevice;
    }

    void PrintTemperatureAndHumidityMeasurement(const GattCharacteristic&, const GattValueChangedEventArgs& args)
    {
        auto reader = DataReader::FromBuffer(args.CharacteristicValue());
        reader.ByteOrder(ByteOrder::LittleEndian);

        std::vector<uint8_t> rawData;
        while (reader.UnconsumedBufferLength() > 0) {
            rawData.push_back(reader.ReadByte());
        }

        // Assuming the format "aa aa a2 00 06 [Temperature] [Temperature] [Humidity] [Humidity] 01 00 [Checksum] 55"
        // with temperature and humidity being two bytes each in Big Endian format.

        // Extract temperature value (assuming bytes 5 and 6 are the temperature)
        int temperatureRaw = (rawData[5] << 8) | rawData[6];
        float temperature = static_cast<float>(temperatureRaw) / 10.0f; // Adjust this divisor based on actual data format

        // Extract humidity value (assuming bytes 7 and 8 are the humidity)
        int humidityRaw = (rawData[7] << 8) | rawData[8];
        float humidity = static_cast<float>(humidityRaw) / 10.0f; // This seems to be correct based on your code

        // Print the raw data
        std::wcout << L"Raw Data: ";
        for (auto byte : rawData) {
            std::wcout << std::hex << std::setfill(L'0') << std::setw(2) << static_cast<int>(byte) << L" ";
        }

        // Print the temperature and humidity values
        std::wcout << std::endl;
        _setmode(_fileno(stdout), _O_U16TEXT); // Set output mode to UTF-16
        std::wcout << L"Temperature: " << temperature << L"°C (" << (temperature * 9 / 5 + 32) << L"°F)" << std::endl;
        std::wcout << L"Humidity: " << humidity << L"%" << std::endl;
    }

    void SubscribeToTemperatureAndHumidityMeasurement(BluetoothLEDevice& device) {
        auto customServiceUuid = BluetoothUuidHelper::FromShortId(0xFFE5);
        auto tempHumidityCharUuid = BluetoothUuidHelper::FromShortId(0xFFE8);

        auto customServiceResult = device.GetGattServicesForUuidAsync(customServiceUuid).get();
        if (customServiceResult.Status() != GattCommunicationStatus::Success) {
            std::wcout << L"Custom service not found." << std::endl;
            return;
        }

        auto customService = customServiceResult.Services().GetAt(0);

        auto tempHumidityCharResult = customService.GetCharacteristicsForUuidAsync(tempHumidityCharUuid).get();
        if (tempHumidityCharResult.Status() != GattCommunicationStatus::Success) {
            std::wcout << L"Temperature and Humidity characteristic not found." << std::endl;
            return;
        }

        auto tempHumidityChar = tempHumidityCharResult.Characteristics().GetAt(0);

        tempHumidityChar.ValueChanged([&](GattCharacteristic sender, GattValueChangedEventArgs args) {
            PrintTemperatureAndHumidityMeasurement(sender, args); // Call the cleaner function to handle data
            });

        auto status = tempHumidityChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        if (status != GattCommunicationStatus::Success) {
            std::wcout << L"Failed to subscribe to Temperature and Humidity notifications." << std::endl;
        }
        else {
            std::wcout << L"Subscribed to Temperature and Humidity notifications." << std::endl;
        }

        // Start the event loop
        while (continueRunning.load()){
            std::this_thread::yield(); // Yield to other threads or processes
        }
    }
};

int main() {
    init_apartment();

    BluetoothLEManager manager;
    manager.StartScanning();
    manager.StopScanning();

    std::wcout << L"Select a device to connect (enter index): ";
    int selectedIndex;
    std::wcin >> selectedIndex;
    manager.ConnectToDevice(selectedIndex);

    return 0;
}
