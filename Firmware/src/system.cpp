#include "system.h"

#include <memory>
#include <string>

#include <libriccore/riccoresystem.h>

#include <HardwareSerial.h>

#include "Config/systemflags_config.h"
#include "Config/commands_config.h"
#include "Config/pinmap_config.h"
#include "Config/general_config.h"
#include "Config/services_config.h"

#include "Loggers/TelemetryLogger/telemetrylogframe.h"

#include "Commands/commands.h"
#include <librnp/default_packets/simplecommandpacket.h>

#include "States/idle.h"

#include "Storage/sdfat_store.h"
#include "Storage/sdfat_file.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
static constexpr int VSPI_BUS_NUM = 0;
static constexpr int HSPI_BUS_NUM = 1;
#else
static constexpr int VSPI_BUS_NUM = VSPI;
static constexpr int HSPI_BUS_NUM = HSPI;
#endif

System::System():
RicCoreSystem(Commands::command_map,Commands::defaultEnabledCommands,Serial),
ThanosR(networkmanager,ChamberPt,OxPt,OxInjPt),
SDSPI(VSPI_BUS_NUM),
SNSRSPI(HSPI_BUS_NUM),
canbus(systemstatus,PinMap::TxCan,PinMap::RxCan,3),
Buck(systemstatus, PinMap::ServoVLog, 1500, 470),
ADC(SNSRSPI, PinMap::ADS_Cs, PinMap::ADS_Clk),
ChamberPt(networkmanager,0),
OxPt(networkmanager,1),
OxInjPt(networkmanager,2),
primarysd(SDSPI,PinMap::SdCs,SD_SCK_MHZ(20),false, &systemstatus)
{};


void System::systemSetup(){
    
    Serial.setRxBufferSize(GeneralConfig::SerialRxSize);
    Serial.begin(GeneralConfig::SerialBaud);

    Serial.println("Test");

    SDSPI.begin(PinMap::V_SCLK,PinMap::V_MISO,PinMap::V_MOSI);
    SDSPI.setFrequency(SD_SCK_MHZ(50));

    pinMode(PinMap::SdCs,OUTPUT);
    digitalWrite(PinMap::SdCs,HIGH);

    //initialize statemachine with idle state
    // statemachine.initalize(std::make_unique<Idle>(systemstatus,commandhandler));
    //any other setup goes here


    pinMode(PinMap::SdCs, OUTPUT);
    pinMode(PinMap::ADS_Cs, OUTPUT);

    digitalWrite(PinMap::SdCs, HIGH);
    digitalWrite(PinMap::ADS_Cs, HIGH);


    pinMode(PinMap::LED, OUTPUT);
    // digitalWrite(PinMap::LED, HIGH); 

    canbus.setup();
    Buck.setup();
    setupSPI();
    ADC.setup();

    ChamberPt.setup();
    OxPt.setup();
    OxInjPt.setup();
 
    networkmanager.setNodeType(NODETYPE::HUB);
    networkmanager.setNoRouteAction(NOROUTE_ACTION::BROADCAST,{1,3});
    networkmanager.addInterface(&canbus);

    uint8_t Engineservice = (uint8_t) Services::ID::Engine;
    networkmanager.registerService(Engineservice,ThanosR.getThisNetworkCallback());


    primarysd.setup();  
    initializeLoggers();

    ThanosR.setup();
    
};

void System::setupSPI(){
    SDSPI.begin(PinMap::V_SCLK,PinMap::V_MISO,PinMap::V_MOSI, PinMap::SdCs);
    SDSPI.setFrequency(2000000);
    SDSPI.setBitOrder(MSBFIRST);
    SDSPI.setDataMode(SPI_MODE0);

    SNSRSPI.begin(PinMap::H_SCLK, PinMap::H_MISO, PinMap::H_MOSI, PinMap::ADS_Cs);
    SNSRSPI.setFrequency(5000000);
    SNSRSPI.setBitOrder(MSBFIRST);
    SNSRSPI.setDataMode(SPI_MODE1);
}



void System::systemUpdate(){
    
    Buck.update();
    ADC.update();
    ThanosR.update(); 

    ChamberPt.update(ADC.getOutput(0));
    OxPt.update(ADC.getOutput(1));
    OxInjPt.update(ADC.getOutput(2));

};

void System::initializeLoggers()
{
    // check if sd card is mounted
    if (primarysd.getState() != StoreBase::STATE::NOMINAL)
    {
        loggerhandler.retrieve_logger<RicCoreLoggingConfig::LOGGERS::SYS>().initialize(nullptr, networkmanager);

        return;
    }

    // open log files
    // get unique directory for logs
    std::string log_directory_path = primarysd.generateUniquePath(log_path, "");
    // make new directory
    primarysd.mkdir(log_directory_path);

    std::unique_ptr<WrappedFile> syslogfile = primarysd.open(log_directory_path + "/syslog.txt", static_cast<FILE_MODE>(O_WRITE | O_CREAT | O_AT_END));
    std::unique_ptr<WrappedFile> telemetrylogfile = primarysd.open(log_directory_path + "/telemetrylog.txt", static_cast<FILE_MODE>(O_WRITE | O_CREAT | O_AT_END),50);

    // intialize sys logger
    loggerhandler.retrieve_logger<RicCoreLoggingConfig::LOGGERS::SYS>().initialize(std::move(syslogfile), networkmanager);

    // initialize telemetry logger
    loggerhandler.retrieve_logger<RicCoreLoggingConfig::LOGGERS::TELEMETRY>().initialize(std::move(telemetrylogfile),[](std::string_view msg){RicCoreLogging::log<RicCoreLoggingConfig::LOGGERS::SYS>(msg);});
}




