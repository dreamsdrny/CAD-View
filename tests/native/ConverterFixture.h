#pragma once
#include <QByteArray>
#include <QVector>

// Keep discovery tests independent of the developer's installed converter/settings.
class ConverterEnvironment {
public:
    ConverterEnvironment() {
        for(const auto* key:{"CAD_ODA_CONVERTER","CAD_LIBREDWG_READER","PATH","ProgramW6432","ProgramFiles","ProgramFiles(x86)"}) {
            values_.append({key,qgetenv(key),qEnvironmentVariableIsSet(key)});
            qputenv(key,"");
        }
    }
    ~ConverterEnvironment() {
        for(const auto& value:values_)
            if(value.present)qputenv(value.key.constData(),value.value);else qunsetenv(value.key.constData());
    }
private:
    struct Value {QByteArray key,value;bool present;};
    QVector<Value> values_;
};
