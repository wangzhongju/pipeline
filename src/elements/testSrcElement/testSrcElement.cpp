#define PL_LOG_ID PL_LOG_TESTSRC
#include "testSrcElement.h"

#include <unistd.h>
#include <yaml-cpp/yaml.h>
#include <cassert>

#include "DataGeneratorFactory.h"

// All the 'prepare...' functions and other helper functions have been moved 
// to their respective generator classes or to the DataGeneratorUtils.cpp file.
// This source file is now much cleaner.

TestSrcElement::~TestSrcElement() {
    if (m_testsrcThread.joinable()) {
        m_testsrcThread.join();
    }
}

app_ret TestSrcElement::Init() {
    if (m_NextElementVec.size() != 1) {
        app_error("TestSrcElement requires exactly one downstream element.");
        return APP_FAILURE;
    }
    app_debug("%s %s\n", " Init : ", m_configFile.c_str());
    
    YAML::Node config;
    try {
        config = YAML::LoadFile(m_configFile);
    } catch (const YAML::Exception& e) {
        app_error("Failed to load or parse config file %s: %s", m_configFile.c_str(), e.what());
        return APP_FAILURE;
    }

    // Use the factory to create the appropriate data generator based on the config
    m_dataGenerator = DataGeneratorFactory::Create(config);

    if (!m_dataGenerator) {
        app_error("Failed to create a data generator. Check config file for 'enable' flags.");
        return APP_FAILURE;
    }

    // Initialize the chosen generator with its specific config section
    return m_dataGenerator->Init(config, m_VBName, mFps, mLoopNum);
}

app_ret TestSrcElement::threadFunc() {
    FILE *fp = NULL;
    
    while (mFrameIndex <= mLoopNum) {
        CBaseMeta *baseMeta = m_dataGenerator->GenerateData(mFrameIndex, fp);

        if (baseMeta) {
            app_debug("%s %s %d\n", " sendframe: ", mName.c_str(), mFrameIndex);
            TransMitToNextToProcess(baseMeta);
        } else {
            // If generator returns null, it might be an error or end of file for some implementations
            // The generator itself should handle EOS logic.
            if (mFrameIndex < mLoopNum) {
                 app_warn("Generator returned null before loop end. Stopping thread.");
                 break;
            }
        }
        
        if (mFrameIndex == mLoopNum) {
            // The last frame has been sent, break the loop.
            // The generator should have sent the EOS flag on this frame.
            break;
        }

        mFrameIndex++;
        usleep(1000000 / mFps);
    }

    if (fp != NULL) {
        fclose(fp);
    }

    app_info("TestSrcElement thread finished.");
    return APP_SUCCESS;
}

app_ret TestSrcElement::Start() {
    app_debug("testsrc element will start\n");
    m_testsrcThread = std::thread(&TestSrcElement::threadFunc, this);
    return APP_SUCCESS;
}

app_ret TestSrcElement::Wait() {
    if (m_testsrcThread.joinable()) {
        m_testsrcThread.join();
    }
    return APP_SUCCESS;
}

app_ret TestSrcElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) { 
    // TestSrc is a source element, it does not process data from other elements.
    return APP_SUCCESS; 
}

app_ret TestSrcElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previousElement) {
    // TestSrc is a source element, it does not process data from other elements.
    return APP_SUCCESS;
}

extern "C" CElement *createEsTestSrcElement(const char *name, const char *path, int fps, int loopnum) {
    return new TestSrcElement(name, path, fps, loopnum);
}