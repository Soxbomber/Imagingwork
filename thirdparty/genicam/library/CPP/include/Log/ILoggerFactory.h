
#ifndef LOG_I_LOGGER_FACTORY_HPP
#define LOG_I_LOGGER_FACTORY_HPP

#include <Log/ILogger.h>

namespace GENICAM_NAMESPACE
{
  class ILoggerFactory
  {
  public:
    virtual ~ILoggerFactory() {};
    virtual const char* GetLoggerFactoryName() = 0;

    // Configure the logger
    // The existing configuration is not cleared or reset
    // Do not remove Loggers when reconfiguring
    // Appenders can be removed
    virtual void ConfigureFromString(const char* configString) = 0;
    virtual void ConfigureDefault() = 0;
    
    // Logger pointer for a particular logger shall not change during runtime
    virtual ILogger* GetLogger(const char* name) = 0;
    virtual bool Exist(const char* name) = 0;
    virtual void PushIndent() = 0;
    virtual void PopIndent() = 0;
  };
}

#endif
