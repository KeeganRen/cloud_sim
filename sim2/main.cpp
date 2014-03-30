#include <stdio.h>
#include <stdlib.h>
#include "tools.h"
#include "system.h"
#include <iostream>

Project project;

int main() {
	for(int i = 0; i < 10000; i++) {
		auto job = new Job();
		job->m_difficulty = randf();
		project.m_jobs.insert(job);
	}

	for(int i = 0; i < 10; i++) {
		auto node = new Node();
		node->m_performance = randf();
		node->m_trust = getRand(0.0f, 0.1f);
		project.addNode(node);
	}

	std::cout << "Simulating..." << std::endl;
	project.simulate();
}