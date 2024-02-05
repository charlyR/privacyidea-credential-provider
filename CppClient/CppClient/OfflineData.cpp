/* * * * * * * * * * * * * * * * * * * * *
**
** Copyright	2019 NetKnights GmbH
** Author:		Nils Behlen
**
**    Licensed under the Apache License, Version 2.0 (the "License");
**    you may not use this file except in compliance with the License.
**    You may obtain a copy of the License at
**
**        http://www.apache.org/licenses/LICENSE-2.0
**
**    Unless required by applicable law or agreed to in writing, software
**    distributed under the License is distributed on an "AS IS" BASIS,
**    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**    See the License for the specific language governing permissions and
**    limitations under the License.
**
** * * * * * * * * * * * * * * * * * * */

#include "OfflineData.h"
#include "Logger.h"
#include <iostream>

using namespace std;

int OfflineData::GetLowestKey()
{
	int lowestKey = INT_MAX;

	for (auto& item : offlineOTPs)
	{
		try
		{
			const int key = stoi(item.first);
			lowestKey = (lowestKey > key ? key : lowestKey);
		}
		catch (const std::invalid_argument & e)
		{
			PIDebug(e.what());
		}
	}

	return lowestKey;
}
