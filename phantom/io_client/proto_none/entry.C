// This file is part of the phantom::io_client::proto_none module.
// Copyright (C) 2010-2012, Eugene Mamchits <mamchits@yandex-team.ru>.
// Copyright (C) 2010-2012, YANDEX LLC.
// This module may be distributed under the terms of the GNU LGPL 2.1.
// See the file ‘COPYING’ or ‘http://www.gnu.org/licenses/lgpl-2.1.html’.

#include "entry.I"
#include "instance.I"

#include <pd/base/exception.H>

namespace phantom { namespace io_client { namespace proto_none {

void entry_t::tasks_t::place(ref_t<task_t> const &task, size_t i, bool flag) {
	size_t j;

	for(; (j = i / 2); i = j) {
		ref_t<task_t> &_task = tasks[j - 1];

		if(task_t::hcmp(_task, task)) break;

		tasks[i - 1] = _task;

		flag = false;
	}

	if(flag) {
		for(; (j = i * 2) <= count; i = j) {
			ref_t<task_t> *_task = &tasks[j - 1];

			if(j < count) {
				ref_t<task_t> *__task = &tasks[j];
				if(!task_t::hcmp(*_task, *__task)) {
					++j;
					_task = __task;
				}
			}

			if(task_t::hcmp(task, *_task)) break;

			tasks[i - 1] = *_task;
		}
	}

	tasks[i - 1] = task;
}

bool entry_t::tasks_t::insert(ref_t<task_t> const &task) {
	if(count && !tasks[0]->active()) {
		place(task, 1, true);
		return false;
	}
	else {
		if(count >= maxcount) {
			maxcount *= 2;
			if(maxcount < 128) maxcount = 128;

			// FIXME: realloc under spinlock

			ref_t<task_t> *_tasks = new ref_t<task_t>[maxcount];

			if(tasks) {
				for(size_t i = 0; i < count; ++i)
					_tasks[i] = tasks[i];

				delete [] tasks;
			}

			tasks = _tasks;
		}

		place(task, ++count, false);
		return true;
	}
}

ref_t<task_t> entry_t::tasks_t::remove() {
	assert(count);

	ref_t<task_t> res = tasks[0];
	ref_t<task_t> &_task = tasks[--count];

	if(count)
		place(_task, 1, true);

	tasks[count] = ref_t<task_t>();

	return res;
}


void entry_t::instances_t::put(instance_t *instance, size_t ind) {
	(instances[ind - 1] = instance)->ind = ind;
}

instance_t *entry_t::instances_t::get(size_t ind) const {
	assert(ind > 0);
	instance_t *instance = instances[ind - 1];
	assert(instance && instance->ind == ind);
	return instance;
}

bool entry_t::instances_t::hcmp(instance_t *instance1, instance_t *instance2) {
	return instance1->rank <= instance2->rank;
}

void entry_t::instances_t::place(instance_t *instance, size_t i, bool flag) {
	size_t j;

	assert(i > 0);

	for(; (j = i / 2); i = j) {
		instance_t *_instance = get(j);

		if(hcmp(_instance, instance)) break;

		put(_instance, i);

		flag = false;
	}

	if(flag) {
		for(; (j = i * 2) <= count; i = j) {
			instance_t *_instance = get(j);

			if(j < count) {
				instance_t *__instance = get(j + 1);
				if(!hcmp(_instance, __instance)) {
					++j;
					_instance = __instance;
				}
			}

			if(hcmp(instance, _instance)) break;

			put(_instance, i);
		}
	}

	put(instance, i);
}


void entry_t::instances_t::insert(instance_t *instance) {
	assert(instance->ind == 0);
	assert(count < max_count);
	place(instance, ++count, false);
}

void entry_t::instances_t::remove(instance_t *instance) {
	instance_t *_instance = get(count--);

	if(_instance != instance) {
		place(_instance, instance->ind, true);
	}

	instance->ind = 0;
}

void entry_t::instances_t::dec_rank(instance_t *instance) {
	assert(instance->ind > 0);
	assert(instance->rank > 0);

	--instance->rank;
	place(instance, instance->ind, true);
}

void entry_t::instances_t::inc_rank(instance_t *instance) {
	assert(instance->ind > 0);

	++instance->rank;
	place(instance, instance->ind, true);
}

void entry_t::insert_instance(instance_t *instance) {
	assert(instance->pending == 0);
	assert(instance->rank == 0);

	bq_cond_guard_t guard(cond);
	instances.insert(instance);
	cond.send();
}

void entry_t::remove_instance(instance_t *instance) {
	bq_cond_guard_t guard(cond);
	instances.remove(instance);
	pending += instance->pending;
	cond.send();
}

void entry_t::derank_instance(instance_t *instance) {
	bq_cond_guard_t guard(cond);
	instances.dec_rank(instance);
	cond.send();
}

void entry_t::run() {
	while(true) {
		instance_t *best = NULL;

		{
			bq_cond_guard_t guard(cond);

			while(
				!pending || instances.get_count() < quorum ||
				(best = instances.head())->rank >= queue_size
			)
				if(!bq_success(cond.wait(NULL)))
					throw exception_sys_t(log::error, errno, "in_cond.wait: %m");

			instances.inc_rank(best);
			--pending;

			best->send_task();
			// !!! two conditions locked here (entry.cond & instance.out_cond) :(
		}
	}
}

void entry_t::stat(out_t &/*out*/, bool /*clear*/) { }

}}} // namespace phantom::io_client::proto_none
