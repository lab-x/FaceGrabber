#include "BufferSynchronizer.h"
#include <thread>
#include <atlstr.h>
BufferSynchronizer::BufferSynchronizer() : 
	m_isDataAvaiable(false)
	//m_printMutex(new std::mutex),
	//m_updateBuffer(new std::mutex),
	//m_isDataAvailableConditionVariable(new std::condition_variable)
{
	
}
BufferSynchronizer::~BufferSynchronizer()
{
	
}

void BufferSynchronizer::onApplicationQuit()
{
	std::unique_lock<std::mutex> lock(m_updateBuffer);
	m_isRunning = false;
	m_isDataAvailableConditionVariable.notify_all();
}

//void BufferSynchronizer::stop() 
//{
//	std::unique_lock<std::mutex> lock(m_updateBuffer);
//	m_isRunning = false;
//	m_isDataAvailableConditionVariable.notify_all();
//}

void BufferSynchronizer::printMessage(std::string msg)
{
	std::lock_guard<std::mutex> lock(m_printMutex);

	auto msgCstring = CString(msg.c_str());
	msgCstring += L"\n";
	OutputDebugString(msgCstring);
}


void BufferSynchronizer::updateThreadFunc()
{
	m_isRunning = true;
	std::chrono::milliseconds dura(80);

	
	while (m_isRunning)
	{
		std::unique_lock<std::mutex> lock(m_updateBuffer);
		while (!m_isDataAvaiable && m_isRunning){
			printMessage("synchronier waiting");
			m_isDataAvailableConditionVariable.wait(lock);
		}
		lock.unlock();
		while (m_isDataAvaiable){
			
			std::vector<pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr> readyPointClouds;
			for (auto bufferWithState : m_bufferWithReadyState){
				printMessage("synchronier pulling data");
				auto cloud = bufferWithState.first->pullData();
				printMessage("synchronier got data");
				if (!cloud){
					printMessage("synchronier received a null cloud");
					m_isDataAvaiable = false;
					break;
				}
				bufferWithState.second = false;
				readyPointClouds.push_back(cloud);
			}

			if (readyPointClouds.size() > 0){
				printMessage("synchronier updating");
				m_numOfFilesToRead--;
				cloudsUpdated(readyPointClouds);
			}

			
			
			printMessage("synchronier sleepig");
			std::chrono::milliseconds dura(80);
			std::this_thread::sleep_for(dura);
			printMessage("synchronier woke up");
		}
		printMessage("synchronier stopping");
		if (m_numOfFilesToRead == 0){
			playbackFinished();
		}
	}
}
void BufferSynchronizer::setBuffer(std::vector<std::shared_ptr<Buffer>> buffers, int numOfFilesToRead)
{
	m_numOfFilesToRead = numOfFilesToRead;
	for (auto buffer : m_bufferWithReadyState){
		buffer.first->dataReady->disconnect_all_slots();
	}
	m_bufferWithReadyState.clear();
	int counter = 0;
	for (int i = 0; i < buffers.size(); i++){
		auto currentBuffer = buffers[i];
		m_bufferWithReadyState.push_back(std::pair<std::shared_ptr<Buffer>, bool>(currentBuffer, false));
		currentBuffer->dataReady->connect(boost::bind(&BufferSynchronizer::signalDataOfBufferWithIndexIsReady, this, i));
	}
}


void BufferSynchronizer::signalDataOfBufferWithIndexIsReady(int index)
{
	printMessage("signal: Data for index now availbel for synchronizer" + index);
	std::unique_lock<std::mutex> lock(m_updateBuffer);
	m_bufferWithReadyState[index].second = true;
	for (auto bufferWithState : m_bufferWithReadyState){
		if (bufferWithState.second == false){
			return;
		}
	}
	printMessage("signal: Data now available for synchronizer");
	m_isDataAvaiable = true;
	m_isDataAvailableConditionVariable.notify_all();
}