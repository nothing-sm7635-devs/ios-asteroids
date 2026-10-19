#pragma once
/* wake: send START STOP UNIT (ACTIVE) if LUN 0 is not ready */
void test_ufs(int wake);
/* take over the host and make LUN 0 ready (SSU ACTIVE if needed); 0 = ready */
int ufs_wake(void);
