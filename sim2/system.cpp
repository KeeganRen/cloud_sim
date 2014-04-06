#include <algorithm>
#include "system.h"
#include "tools.h"
#include <iostream>
#include <assert.h>

Job::Job() 
{
	m_difficulty = 1.0f;

	m_bestCorrectness = 0;
	m_assumedCorrectness = 0;
}

float Job::getCorrectness() const
{
	return m_assumedCorrectness + m_bestCorrectness;
}

void Job::nodeStarted( AssumedResult* res )
{
	m_assumedCorrectness += res->correctness;
	m_assumedResults[res->node] = res;
}

Result* Job::workDone( AssumedResult* res, int hash )
{
	m_assumedResults.erase(res->node);
	m_assumedCorrectness -= res->correctness;
	assert(m_assumedCorrectness >= -0.01f);
	if(m_bestCorrectness >= 1.0f) {
		//This job was already done, this is late send (?)
		return NULL;
	}

	Result* result = new Result();
	result->hash = hash;
	result->node = res->node;
	result->job = this;
	result->correctness = res->correctness;

	m_results.insert(std::pair<int, Result*>(hash, result));
	auto it = m_correctnessPerHash.find(hash);
	float resultCor = result->correctness;
	if(it != m_correctnessPerHash.end()) {
		resultCor += it->second;
	}

	m_correctnessPerHash[hash] = resultCor;

	if(resultCor > m_bestCorrectness) {
		m_bestCorrectness = resultCor;
	}

	/*
	//Hand out trust for everyone for this confirmation
	for(auto it = m_results.begin(); it != m_results.end(); ++it) {
		auto itres = it->second;
		auto node = res->node;

		if(itres != result && result->hash == itres->hash) {
			node->m_trust += result->correctness;
			result->node->m_trust += itres->correctness;
		}
	}
	*/

	//Job is now done, hand out trust
	if(m_bestCorrectness >= 1.0f) {
		//This job is done now, add trust to all participants
		for(auto it = m_results.begin(); it != m_results.end(); ++it) {
			auto res = it->second;
			auto node = res->node;

			node->m_trust += m_bestCorrectness - res->correctness;
		}
	}

	return result;
}

Node::Node() 
{
	m_trust = 0;
	m_performance = 0.0f;

	m_nextActionTime = 0;
	m_currentWork = NULL;
}

bool Node::hasSubmitted( Job* job )
{
	return m_resultsJob.find(job) != m_resultsJob.end();
}

void Node::startJob( Job* job, float corr, uint64_t currentTick )
{
	AssumedResult* assumed = new AssumedResult();
	assumed->job = job;
	assumed->node = this;
	assumed->correctness = corr;

	job->nodeStarted(assumed);

	m_currentWork = assumed;

	m_nextActionTime = currentTick + 1 + (uint64_t)(100.f * job->m_difficulty * (1.0f - m_performance));
}

void Node::endJob()
{
	auto res = m_currentWork->job->workDone(m_currentWork, 0);
	if(res) {
		m_results.push_back(res);
		m_resultsJob.insert(res->job);
	}
	m_currentWork = NULL;
}

float round2(float f,float pres)
{
	return (float) (floor(f*(1.0f/pres) + 0.5)/(1.0f/pres));
}

bool JobCompare::operator()( const Job* a, const Job* b ) const
{
	if(a == b) {
		return false;
	}

	if(a->m_active == b->m_active) {
		float ac = a->getCorrectness();
		float bc = b->getCorrectness();
		if(ac == bc) {
			return a < b;
		}

		return ac > bc;
	} else {
		return a->m_active;
	}
}

bool NodeCompare::operator()( const Node* a, const Node* b ) const
{
	if(a == b) {
		return false;
	}

	if(a->m_nextActionTime == b->m_nextActionTime) {
		return a < b; //compare ptrs
	}

	return a->m_nextActionTime < b->m_nextActionTime;
}

Project::Project()
{
	m_bestTrust = 0.0f;
}

void Project::addNode( Node* node )
{
	m_nodes.insert(node);
	updateTrust(node);

}

Job* Project::findJobForNode( Node* node )
{
	Job search;
	search.m_assumedCorrectness = clamp(getRand(1.0f, 1.3f) - getTrust(node), 0.0f, 1.0f);
	search.m_active = true;

	auto it = std::lower_bound(m_jobs.begin(), m_jobs.end(), &search, JobCompare());
	for(; it != m_jobs.end(); ++it) {
		auto obj = *it;
		if(!obj->m_active) {
			break;
		}

		if(obj->getCorrectness() >= 1.0f) {
			continue;
		}

		if(node->hasSubmitted(obj)) {
			continue;
		}

		m_jobs.erase(it);
		return obj;
	}

	search.m_assumedCorrectness = 1.0f;

	it = std::lower_bound(m_jobs.begin(), m_jobs.end(), &search, JobCompare());
	for(; it != m_jobs.end(); ++it) {
		auto obj = *it;
		if(!obj->m_active) {
			break;
		}

		if(obj->getCorrectness() >= 1.0f) {
			continue;
		}

		if(node->hasSubmitted(obj)) {
			continue;
		}

		m_jobs.erase(it);
		return obj;
	}

	return NULL;
}

float Project::getTrust( Node* node )
{
	float constTrust = 0.1f;

	if(m_bestTrust == 0.0f) {
		return constTrust;
	}

	assert(node->m_trust <= m_bestTrust);
	return clamp(constTrust + node->m_trust / m_bestTrust, 0.0f, 1.0f);
}

void Project::updateTrust( Node* node )
{
	if(node->m_trust > m_bestTrust) {
		m_bestTrust = node->m_trust;
	}
}

#include "../gnuplot-iostream/gnuplot-iostream.h"

void Project::activateJob()
{
	auto it = m_jobs.rbegin();
	auto obj = *it;

	if(obj->m_active) {
		return;
	}

	m_jobs.erase(--(it.base()));
	obj->m_active = true;
	m_jobs.insert(obj);
}

void Project::simulate()
{
	Gnuplot gp;

	std::vector<std::pair<uint64_t, float> > xy_pts_A;
	std::vector<std::pair<uint64_t, float> > xy_pts_B;
	std::vector<std::pair<uint64_t, float> > max_trust;
	std::vector<std::pair<uint64_t, float> > job_gets;

	uint64_t currentTick = 0;
	int resultsSent = 0;
	int jobsDone = 0;

	auto node_it = m_nodes.begin();
	//std::advance(node_it, 1);
	Node* tested_node = *node_it;

	std::set<Node*> nodesToReinsert;
	for(;; currentTick++) {
		for(auto it = m_nodes.begin(); it != m_nodes.end(); ) {
			auto node = *it;
			if(node->m_nextActionTime > currentTick) {
				break;
			}

			m_nodes.erase(it++);

			if(node->m_currentWork) {
				Job* currentJob = node->m_currentWork->job;
				auto job_it = m_jobs.find(currentJob);
				m_jobs.erase(job_it);
				
				node->endJob();

				m_jobs.insert(currentJob);

				resultsSent++;
				if(currentJob->m_bestCorrectness >= 1.0f) {
					activateJob();
					jobsDone++;
				}


				for(auto node_it = currentJob->m_results.begin(); node_it != currentJob->m_results.end(); ++node_it) {
					updateTrust(node_it->second->node);
				}

				if(node == tested_node) {
					printf("node hands out work, now trust %d %g (%d/%d)\n", currentJob->m_results.size(), getTrust((tested_node)),
						jobsDone, m_jobs.size());
				}
			}

			auto job = findJobForNode(node);
			if(job) {
				if(node == tested_node) {
					job_gets.push_back(std::pair<uint64_t, float>(currentTick, getTrust(node)));
					printf("tick %lld, node gets work, res %d, %g, %g\n", currentTick, job->m_results.size(), job->m_bestCorrectness, job->m_assumedCorrectness);
				}

				node->startJob(job, getTrust(node) + getRand(0.0f, 0.1f), currentTick);
				m_jobs.insert(job);
			} else {
				//no job - delay a tick
				node->m_nextActionTime = currentTick;
			}
			
			nodesToReinsert.insert(node);
		}

		if(!nodesToReinsert.empty()) {
			for(auto it = nodesToReinsert.begin(); it != nodesToReinsert.end(); ++it) {
				addNode(*it);
			}

			nodesToReinsert.clear();
		}

		xy_pts_B.push_back(std::pair<uint64_t, float>(currentTick, getTrust(tested_node)));
		//xy_pts_A.push_back(std::pair<uint64_t, float>(currentTick, tested_node->m_trust));
		//max_trust.push_back(std::pair<uint64_t, float>(currentTick, m_bestTrust));
		//printf("%g tick %lld (%d, %d)\n", getTrust(tested_node), currentTick, jobsDone, resultsSent);

		if(jobsDone >= (int)m_jobs.size()) {
			break;
		}
	}

	printf("DONE, after tick %lld (%d, %d)\n", currentTick, jobsDone, resultsSent);

	gp << "plot";
	//gp << gp.file1d(xy_pts_A) << "with lines title 'abs_trust', ";
	//gp << gp.file1d(max_trust) << "with lines title 'max', ";
	gp << gp.file1d(xy_pts_B) << "with lines title 'trust', ";
	gp << gp.file1d(job_gets) << "with points title 'jobs'";
	gp << std::endl;

#ifdef _WIN32
	// For Windows, prompt for a keystroke before the Gnuplot object goes out of scope so that
	// the gnuplot window doesn't get closed.
	std::cout << "Press enter to exit." << std::endl;
	std::cin.get();
#endif
}
