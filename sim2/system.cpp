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

	m_nextActionTime = currentTick + 1 + (uint64_t)(10.f * job->m_difficulty * (1.0f - m_performance));
}

void Node::endJob()
{
	auto res = m_currentWork->job->workDone(m_currentWork, 0);
	m_results.push_back(res);
	m_resultsJob.insert(res->job);
	m_currentWork = NULL;
}

bool JobCompare::operator()( const Job* a, const Job* b ) const
{
	if(a == b) {
		return false;
	}

	float ac = a->getCorrectness();
	float bc = b->getCorrectness();
	if(ac == bc) {
		return a < b;
	}

	return ac > bc;
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
	if(node->m_trust > m_bestTrust) {
		m_bestTrust = node->m_trust;
	}
}

Job* Project::findJobForNode( Node* node )
{
	Job search;
	search.m_assumedCorrectness = clamp(getRand(1.0f, 1.2f) - getTrust(node), 0.0f, 1.0f);
	
	auto it = std::lower_bound(m_jobs.begin(), m_jobs.end(), &search, JobCompare());
	for(; it != m_jobs.end(); ++it) {
		auto obj = *it;
		if(obj->getCorrectness() >= 1.0f) {
			continue;
		}

		if(node->hasSubmitted(obj)) {
			continue;
		}

		m_jobs.erase(it);
		return obj;
	}
	
	for(auto rit = m_jobs.rbegin(); rit != m_jobs.rend(); ++rit) {
		auto obj = *rit;
		if(obj->getCorrectness() >= 1.0f) {
			continue;
		}

		if(node->hasSubmitted(obj)) {
			return NULL;
		}

		m_jobs.erase(--(rit.base()));
		return obj;
	}
	return NULL;
}

float Project::getTrust( Node* node )
{
	if(m_bestTrust == 0.0f) {
		return 0.0f;
	}
	return node->m_trust / m_bestTrust;
}

void Project::simulate()
{
	uint64_t currentTick = 0;
	int resultsSent = 0;
	int jobsDone = 0;

	for(;;) {
		std::set<Node*> nodesToReinsert;
		for(auto it = m_nodes.begin(); it != m_nodes.end(); ) {
			auto node = *it;
			if(node->m_nextActionTime > currentTick) {
				currentTick = node->m_nextActionTime;
				printf("tick %lld (%d, %d)\n", currentTick, jobsDone, resultsSent);
				break;
			}

			if(node->m_currentWork) {
				Job* currentJob = node->m_currentWork->job;
				auto job_it = m_jobs.find(currentJob);
				m_jobs.erase(job_it);
				
				node->endJob();
				m_jobs.insert(currentJob);
				resultsSent++;
				if(currentJob->m_bestCorrectness >= 1.0f) {
					jobsDone++;
				}
			}

			auto job = findJobForNode(node);
			if(job) {
				node->startJob(job, getTrust(node), currentTick);
				m_jobs.insert(job);
			} else {
				//no job - delay a tick
				node->m_nextActionTime = currentTick + 1;
			}
			
			m_nodes.erase(it++);
			nodesToReinsert.insert(node);
		}

		for(auto it = nodesToReinsert.begin(); it != nodesToReinsert.end(); ++it) {
			addNode(*it);
		}

		if(jobsDone >= m_jobs.size()) {
			break;
		}
	}
	printf("DONE, after tick %lld (%d, %d)\n", currentTick, jobsDone, resultsSent);
}
