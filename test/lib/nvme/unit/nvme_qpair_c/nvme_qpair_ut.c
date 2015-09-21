/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2015 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdbool.h>
#include "nvme/nvme_internal.h"

#include "CUnit/Basic.h"

#include "nvme/nvme_qpair.c"

struct nvme_driver g_nvme_driver = {
	.lock = NVME_MUTEX_INITIALIZER,
	.max_io_queues = NVME_MAX_IO_QUEUES,
};

int32_t nvme_retry_count = 1;

char outbuf[OUTBUF_SIZE];

bool fail_vtophys = false;

uint64_t nvme_vtophys(void *buf)
{
	if (fail_vtophys) {
		return (uint64_t) - 1;
	} else {
		return (uintptr_t)buf;
	}
}

void nvme_dump_completion(struct nvme_completion *cpl)
{
}

void prepare_for_test(void)
{
}

void
test1(void)
{
	struct nvme_qpair qpair = {};
	struct nvme_command cmd = {};

	outbuf[0] = '\0';

	/*
	 * qpair.id == 0 means it is an admin queue.  Ensure
	 *  that the opc is decoded as an admin opc and not an
	 *  I/o opc.
	 */
	qpair.id = 0;
	cmd.opc = NVME_OPC_IDENTIFY;

	nvme_qpair_print_command(&qpair, &cmd);

	CU_ASSERT(strstr(outbuf, "IDENTIFY") != NULL);
}

void
test2(void)
{
	struct nvme_qpair qpair = {};
	struct nvme_command cmd = {};

	outbuf[0] = '\0';

	/*
	 * qpair.id != 0 means it is an I/O queue.  Ensure
	 *  that the opc is decoded as an I/O opc and not an
	 *  admin opc.
	 */
	qpair.id = 1;
	cmd.opc = NVME_OPC_DATASET_MANAGEMENT;

	nvme_qpair_print_command(&qpair, &cmd);

	CU_ASSERT(strstr(outbuf, "DATASET MANAGEMENT") != NULL);
}

void
prepare_submit_request_test(struct nvme_qpair *qpair,
			    struct nvme_controller *ctrlr,
			    struct nvme_registers *regs)
{
	memset(ctrlr, 0, sizeof(*ctrlr));
	ctrlr->regs = regs;
	nvme_qpair_construct(qpair, 1, 128, 32, ctrlr);

	CU_ASSERT(qpair->sq_tail == 0);
	CU_ASSERT(qpair->cq_head == 0);

	fail_vtophys = false;
}

void
cleanup_submit_request_test(struct nvme_qpair *qpair)
{
	nvme_qpair_destroy(qpair);
}

void
expected_success_callback(void *arg, const struct nvme_completion *cpl)
{
	CU_ASSERT(!nvme_completion_is_error(cpl));
}

void
expected_failure_callback(void *arg, const struct nvme_completion *cpl)
{
	CU_ASSERT(nvme_completion_is_error(cpl));
}

void
test3(void)
{
	struct nvme_qpair	qpair = {};
	struct nvme_request	*req;
	struct nvme_controller	ctrlr = {};
	struct nvme_registers	regs = {};

	prepare_submit_request_test(&qpair, &ctrlr, &regs);

	nvme_alloc_request(&req);
	req->payload_size = 4096;
	req->cb_fn = expected_success_callback;

	CU_ASSERT(qpair.sq_tail == 0);

	nvme_qpair_submit_request(&qpair, req);

	CU_ASSERT(qpair.sq_tail == 1);

	cleanup_submit_request_test(&qpair);
	nvme_free_request(req);
}

void
test4(void)
{
	struct nvme_qpair	qpair = {};
	struct nvme_request	*req;
	struct nvme_controller	ctrlr = {};
	struct nvme_registers	regs = {};

	prepare_submit_request_test(&qpair, &ctrlr, &regs);

	nvme_alloc_request(&req);
	req->payload_size = 4096;
	req->cb_fn = expected_failure_callback;

	/* Force vtophys to return a failure.  This should
	 *  result in the nvme_qpair manually failing
	 *  the request with error status to signify
	 *  a bad payload buffer.
	 */
	fail_vtophys = true;
	outbuf[0] = '\0';

	CU_ASSERT(qpair.sq_tail == 0);

	nvme_qpair_submit_request(&qpair, req);

	CU_ASSERT(qpair.sq_tail == 0);
	/* Assert that command/completion data was printed to log. */
	CU_ASSERT(strlen(outbuf) > 0);

	cleanup_submit_request_test(&qpair);
}

void
test_ctrlr_failed(void)
{
	struct nvme_qpair	qpair = {};
	struct nvme_request	*req;
	struct nvme_controller	ctrlr = {};
	struct nvme_registers	regs = {};

	prepare_submit_request_test(&qpair, &ctrlr, &regs);

	nvme_alloc_request(&req);
	req->payload_size = 4096;
	req->cb_fn = expected_failure_callback;

	/* Disable the queue and set the controller to failed.
	 * Set the controller to resetting so that the qpair won't get re-enabled.
	 */
	qpair.is_enabled = false;
	ctrlr.is_failed = true;
	ctrlr.is_resetting = true;

	outbuf[0] = '\0';

	CU_ASSERT(qpair.sq_tail == 0);

	nvme_qpair_submit_request(&qpair, req);

	CU_ASSERT(qpair.sq_tail == 0);
	/* Assert that command/completion data was printed to log. */
	CU_ASSERT(strlen(outbuf) > 0);

	cleanup_submit_request_test(&qpair);
}

void struct_packing(void)
{
	/* ctrlr is the first field in nvme_qpair after the fields
	 * that are used in the I/O path. Make sure the I/O path fields
	 * all fit into two cache lines.
	 */
	CU_ASSERT(offsetof(struct nvme_qpair, ctrlr) <= 128);
}

void test_nvme_qpair_fail(void)
{
	struct nvme_qpair	qpair = {0};
	struct nvme_request	*req = NULL;
	struct nvme_controller	ctrlr = {};
	struct nvme_registers	regs = {0};
	struct nvme_tracker	*tr_temp;
	uint64_t		phys_addr = 0;

	prepare_submit_request_test(&qpair, &ctrlr, &regs);

	tr_temp = nvme_malloc("nvme_tracker", sizeof(struct nvme_tracker),
			      64, &phys_addr);
	tr_temp->req = NULL;
	nvme_alloc_request(&tr_temp->req);

	LIST_INSERT_HEAD(&qpair.outstanding_tr, tr_temp, list);
	nvme_qpair_fail(&qpair);
	CU_ASSERT_TRUE(LIST_EMPTY(&qpair.outstanding_tr));

	nvme_alloc_request(&req);
	STAILQ_INSERT_HEAD(&qpair.queued_req, req, stailq);
	nvme_qpair_fail(&qpair);
	CU_ASSERT_TRUE(STAILQ_EMPTY(&qpair.queued_req));

	cleanup_submit_request_test(&qpair);
}

void test_nvme_qpair_process_completions(void)
{
	struct nvme_qpair	qpair = {0};
	struct nvme_request	*req = NULL;
	struct nvme_controller	ctrlr = {};
	struct nvme_registers	regs = {0};

	prepare_submit_request_test(&qpair, &ctrlr, &regs);
	qpair.is_enabled = false;
	qpair.ctrlr->is_resetting = true;

	nvme_qpair_process_completions(&qpair);
	cleanup_submit_request_test(&qpair);
}

void test_nvme_qpair_destroy(void)
{
	struct nvme_qpair	qpair = {0};
	struct nvme_request	*req = NULL;
	struct nvme_controller	ctrlr = {};
	struct nvme_registers	regs = {0};
	struct nvme_tracker	*tr_temp;
	uint64_t		phys_addr = 0;

	memset(&ctrlr, 0, sizeof(ctrlr));
	ctrlr.regs = &regs;
	nvme_qpair_construct(&qpair, 1, 128, 32, &ctrlr);
	qpair.cmd = nvme_malloc("nvme_command", sizeof(struct nvme_command),
				64, &phys_addr);

	qpair.cpl = nvme_malloc("nvme_completion", sizeof(struct nvme_completion),
				64, &phys_addr);

	qpair.act_tr = nvme_malloc("nvme_tracker", sizeof(struct nvme_tracker),
				   64, &phys_addr);

	nvme_qpair_destroy(&qpair);
}

void test_nvme_completion_is_retry(void)
{
	struct nvme_completion	*cpl = NULL;
	uint64_t		phys_addr = 0;
	bool			ret_val = false;

	cpl = nvme_malloc("nvme_completion", sizeof(struct nvme_completion),
			  64, &phys_addr);

	cpl->status.sct = NVME_SCT_GENERIC;
	cpl->status.sc = NVME_SC_ABORTED_BY_REQUEST;
	cpl->status.dnr = 0;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_TRUE(ret_val);

	cpl->status.sc = NVME_SC_INVALID_OPCODE;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_INVALID_FIELD;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_COMMAND_ID_CONFLICT;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_DATA_TRANSFER_ERROR;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_ABORTED_POWER_LOSS;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_INTERNAL_DEVICE_ERROR;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_ABORTED_FAILED_FUSED;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_ABORTED_MISSING_FUSED;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_INVALID_NAMESPACE_OR_FORMAT;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_COMMAND_SEQUENCE_ERROR;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_LBA_OUT_OF_RANGE;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = NVME_SC_CAPACITY_EXCEEDED;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sc = 0x70;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sct = NVME_SCT_COMMAND_SPECIFIC;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sct = NVME_SCT_MEDIA_ERROR;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sct = NVME_SCT_VENDOR_SPECIFIC;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);

	cpl->status.sct = 0x4;
	ret_val = nvme_completion_is_retry(cpl);
	CU_ASSERT_FALSE(ret_val);


}

int main(int argc, char **argv)
{
	CU_pSuite	suite = NULL;
	unsigned int	num_failures;

	if (CU_initialize_registry() != CUE_SUCCESS) {
		return CU_get_error();
	}

	suite = CU_add_suite("nvme_qpair", NULL, NULL);
	if (suite == NULL) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	if (
		CU_add_test(suite, "test1", test1) == NULL
		|| CU_add_test(suite, "test2", test2) == NULL
		|| CU_add_test(suite, "test3", test3) == NULL
		|| CU_add_test(suite, "test4", test4) == NULL
		|| CU_add_test(suite, "ctrlr_failed", test_ctrlr_failed) == NULL
		|| CU_add_test(suite, "struct_packing", struct_packing) == NULL
		|| CU_add_test(suite, "nvme_qpair_fail", test_nvme_qpair_fail) == NULL
		|| CU_add_test(suite, "nvme_qpair_process_completions", test_nvme_qpair_process_completions) == NULL
		|| CU_add_test(suite, "nvme_qpair_destroy", test_nvme_qpair_destroy) == NULL
		|| CU_add_test(suite, "nvme_completion_is_retry", test_nvme_completion_is_retry) == NULL
	) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	num_failures = CU_get_number_of_failures();
	CU_cleanup_registry();
	return num_failures;
}