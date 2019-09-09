// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBE_WRITE_ENTITY_HELPER_H
#define KBE_WRITE_ENTITY_HELPER_H

// common include	
// #define NDEBUG
#include <sstream>
#include "common.h"
#include "sqlstatement.h"
#include "common/common.h"
#include "common/memorystream.h"
#include "helper/debug_helper.h"
#include "db_interface/db_interface.h"
#include "db_interface/entity_table.h"
#include "db_interface_mysql.h"

namespace KBEngine{ 
	typedef std::vector<KBEShared_ptr< mysql::DBContext > > TABLE_DATAS;
	typedef KBEUnordered_map< std::string, TABLE_DATAS> TABLE_DATAS_MAP;

class WriteEntityHelper
{
public:
	WriteEntityHelper()
	{
	}

	virtual ~WriteEntityHelper()
	{
	}

	static SqlStatement* createSql(DBInterface* pdbi, DB_TABLE_OP opType, 
		std::string tableName, DBID parentDBID, 
		DBID dbid, mysql::DBContext::DB_ITEM_DATAS& tableVal)
	{
		DEBUG_MSG(fmt::format("huyf-yh: createSql-1 opType={} tableName={} parentDBID={} dbid={}\n\t", opType, tableName, parentDBID, dbid));

		SqlStatement* pSqlcmd = NULL;

		switch(opType)
		{
		case TABLE_OP_UPDATE:
			if(dbid > 0)
				pSqlcmd = new SqlStatementUpdate(pdbi, tableName, parentDBID, dbid, tableVal);
			else
				pSqlcmd = new SqlStatementInsert(pdbi, tableName, parentDBID, dbid, tableVal);
			break;
		case TABLE_OP_INSERT:
			pSqlcmd = new SqlStatementInsert(pdbi, tableName, parentDBID, dbid, tableVal);
			break;
		case TABLE_OP_DELETE:
			break;
		default:
			KBE_ASSERT(false && "no support!\n");
		};

		DEBUG_MSG(fmt::format("huyf-yh: createSql-2 dbid={} sqlstr_={}\n\t", pSqlcmd->dbid(),  pSqlcmd->sql().c_str()));
		return pSqlcmd;
	}

	static std::string getTableValues(mysql::DBContext& context)
	{
		mysql::DBContext::DB_ITEM_DATAS& tableItemDatas = context.items;
		std::stringstream ss;
		if (context.parentTableDBID)
			ss << "(" << context.dbid << "," << context.parentTableDBID;
		else
			ss << "(" << context.dbid;
		
		size_t nColumn = tableItemDatas.size();
		if (nColumn)
			ss << ",";

		for (size_t i=0; i<nColumn; ++i)
		{
			KBEShared_ptr<mysql::DBContext::DB_ITEM_DATA> pSotvs = tableItemDatas[i];
			if (pSotvs->extraDatas.size() > 0)
				ss << pSotvs->extraDatas;
			else
				ss << pSotvs->sqlval;

			if(i!=nColumn-1)
				ss << ",";
		}
		ss << ")";
		return ss.str();
	}

	static std::string getDuplicateKeyUpdateStmt(mysql::DBContext& context)
	{
		mysql::DBContext::DB_ITEM_DATAS& tableItemDatas = context.items;
		
		std::stringstream ss;
		if(context.parentTableDBID)
			ss << TABLE_PARENTID_CONST_STR << "=values(" << TABLE_PARENTID_CONST_STR << ")";

		if (!tableItemDatas.empty())
			ss << ",";
		
		size_t nColumn = tableItemDatas.size();
		for (size_t i = 0; i<nColumn; ++i)
		{
			KBEShared_ptr<mysql::DBContext::DB_ITEM_DATA> pSotvs = tableItemDatas[i];
			ss << pSotvs->sqlkey<< "=values("<< pSotvs->sqlkey <<")";
			
			if (i != nColumn - 1)
				ss<<",";
		}
		return ss.str();
	}

	static std::string getTableColumnNames(mysql::DBContext& context)
	{
		mysql::DBContext::DB_ITEM_DATAS& tableItemDatas = context.items;

		std::stringstream ss;
		if(context.parentTableDBID)
			ss << "(" << TABLE_ID_CONST_STR << "," << TABLE_PARENTID_CONST_STR ;
		else
			ss << "(" << TABLE_ID_CONST_STR ;

		size_t nColumn = tableItemDatas.size();
		if (nColumn)
			ss << ",";

		for (size_t i = 0; i<nColumn; ++i)
		{
			KBEShared_ptr<mysql::DBContext::DB_ITEM_DATA> pSotvs = tableItemDatas[i];
			ss << pSotvs->sqlkey;

			if (i != nColumn - 1)
				ss << ",";
		}
		ss << ")";
		return ss.str();
	}

	static int writeDBBatchly(DB_TABLE_OP optype, DBInterface* pdbi, mysql::DBContext& parentContext, std::string childTableName, TABLE_DATAS& childDataContexts, std::vector<DBID>& dbids)
	{
		size_t nData = std::min(childDataContexts.size(), dbids.size());
		if(nData==0)
			return 0;

		mysql::DBContext& firstContext = *(childDataContexts[0].get());
		if (firstContext.items.empty())
			return 0;

		firstContext.parentTableDBID = parentContext.dbid;
		std::string sqlstrInsert = "insert into " ENTITY_TABLE_PERFIX "_" + childTableName + " " + getTableColumnNames(firstContext) + " values ";

		for (size_t i = 0;i < nData;++i)
		{
			mysql::DBContext& childContext = *(childDataContexts[i].get());
			childContext.parentTableDBID = parentContext.dbid;
			childContext.dbid = dbids[i];
			sqlstrInsert += getTableValues(childContext);
			if (i != nData - 1)
				sqlstrInsert += ",";
		}

		sqlstrInsert += " on duplicate key update " + getDuplicateKeyUpdateStmt(firstContext);
		if (pdbi->query(sqlstrInsert.c_str(), sqlstrInsert.size(), false))
		{
			for (size_t i = 0;i < nData;++i)
			{
				mysql::DBContext& childContext = *(childDataContexts[i].get());
				writeDB(optype, pdbi, childContext, false);
			}
		}
		else {
			ERROR_MSG(fmt::format("writeDBBatchly::query: {}\n\tsql:{}\n", pdbi->getstrerror(), sqlstrInsert));
			return -1;
		}
		return nData;
	}

	/**
		�����ݸ��µ�����
	*/
	static bool writeDB(DB_TABLE_OP optype, DBInterface* pdbi, mysql::DBContext& context, bool writeCurContext = true)
	{
		bool ret = true;

		if (!context.isEmpty && writeCurContext)
		{
			SqlStatement* pSqlcmd = createSql(pdbi, optype, context.tableName,
				context.parentTableDBID,
				context.dbid, context.items);
			
			DEBUG_MSG(fmt::format("huyf-yh: WriteEntityHelper::writeDB::tableName=[{}] optype=[{}] items.size=[{}] Sql=[{}]\n", 
				optype, context.tableName, context.items.size(), pSqlcmd->sql().c_str()));

			ret = pSqlcmd->query();
			context.dbid = pSqlcmd->dbid();
			delete pSqlcmd;
		}

		if (optype == TABLE_OP_INSERT)
		{
			// ��ʼ�������е��ӱ�
			mysql::DBContext::DB_RW_CONTEXTS::iterator iter1 = context.optable.begin();
			for (; iter1 != context.optable.end(); ++iter1)
			{
				mysql::DBContext& wbox = *iter1->second.get();

				// �󶨱��ϵ
				wbox.parentTableDBID = context.dbid;

				// �����ӱ�
				writeDB(optype, pdbi, wbox);
			}
		}
		else
		{
			// ����и�ID���ȵõ����������ݿ���ͬ��id�������ж�����Ŀ�� ��ȡ��ÿ�����ݵ�id
			// Ȼ���ڴ��е�����˳����������ݿ⣬ ������ݿ����д��ڵ���Ŀ��˳�򸲸Ǹ������е���Ŀ�� �����������
			// �������ݿ������е���Ŀ�����ʣ������ݣ� ��������������ݿ��е���Ŀ��ɾ�����ݿ��е���Ŀ
			// select id from tbl_SpawnPoint_xxx_values where parentID = 7;
			KBEUnordered_map< std::string, std::vector<DBID> > childTableDBIDs;
			TABLE_DATAS_MAP  childTableDatas;

			if (context.dbid > 0)
			{
				mysql::DBContext::DB_RW_CONTEXTS::iterator iter1 = context.optable.begin();
				for (; iter1 != context.optable.end(); ++iter1)
				{
					mysql::DBContext& wbox = *iter1->second.get();

					KBEUnordered_map<std::string, std::vector<DBID> >::iterator iter =
						childTableDBIDs.find(context.tableName);

					if (iter == childTableDBIDs.end())
					{
						std::vector<DBID> v;
						childTableDBIDs.insert(std::pair< std::string, std::vector<DBID> >(wbox.tableName, v));

						TABLE_DATAS dataVec;
						childTableDatas.insert(std::pair < std::string, TABLE_DATAS>(wbox.tableName, dataVec));
					}

					childTableDatas[wbox.tableName].push_back(iter1->second);
				}

				if (childTableDBIDs.size() > 1)
				{
					std::string sqlstr_getids;
					KBEUnordered_map< std::string, std::vector<DBID> >::iterator tabiter = childTableDBIDs.begin();
					for (; tabiter != childTableDBIDs.end();)
					{
						char sqlstr[MAX_BUF * 10];
						kbe_snprintf(sqlstr, MAX_BUF * 10, "select count(id) from " ENTITY_TABLE_PERFIX "_%s where " TABLE_PARENTID_CONST_STR "=%" PRDBID " union all ",
							tabiter->first.c_str(),
							context.dbid);

						sqlstr_getids += sqlstr;

						kbe_snprintf(sqlstr, MAX_BUF * 10, "select id from " ENTITY_TABLE_PERFIX "_%s where " TABLE_PARENTID_CONST_STR "=%" PRDBID,
							tabiter->first.c_str(),
							context.dbid);

						sqlstr_getids += sqlstr;
						if (++tabiter != childTableDBIDs.end())
							sqlstr_getids += " union all ";
					}

					if (pdbi->query(sqlstr_getids.c_str(), sqlstr_getids.size(), false))
					{
						MYSQL_RES * pResult = mysql_store_result(static_cast<DBInterfaceMysql*>(pdbi)->mysql());
						if (pResult)
						{
							MYSQL_ROW arow;
							int32 count = 0;
							tabiter = childTableDBIDs.begin();
							bool first = true;

							while ((arow = mysql_fetch_row(pResult)) != NULL)
							{
								if (count == 0)
								{
									StringConv::str2value(count, arow[0]);
									if (!first || count <= 0)
										tabiter++;
									continue;
								}

								DBID old_dbid;
								StringConv::str2value(old_dbid, arow[0]);
								tabiter->second.push_back(old_dbid);
								count--;
								first = false;
							}

							mysql_free_result(pResult);
						}
					}
				}
				else if (childTableDBIDs.size() == 1)
				{
					KBEUnordered_map< std::string, std::vector<DBID> >::iterator tabiter = childTableDBIDs.begin();
					char sqlstr[MAX_BUF * 10];
					kbe_snprintf(sqlstr, MAX_BUF * 10, "select id from " ENTITY_TABLE_PERFIX "_%s where " TABLE_PARENTID_CONST_STR "=%" PRDBID,
						tabiter->first.c_str(),
						context.dbid);

					if (pdbi->query(sqlstr, strlen(sqlstr), false))
					{
						MYSQL_RES * pResult = mysql_store_result(static_cast<DBInterfaceMysql*>(pdbi)->mysql());
						if (pResult)
						{
							MYSQL_ROW arow;
							while ((arow = mysql_fetch_row(pResult)) != NULL)
							{
								DBID old_dbid;
								StringConv::str2value(old_dbid, arow[0]);
								tabiter->second.push_back(old_dbid);
							}

							mysql_free_result(pResult);
						}
					}
				}
			}

			// �����Ҫ��մ˱� ��ѭ��N���Ѿ��ҵ���dbid�� ʹ���ӱ��е��ӱ�Ҳ����Чɾ��
			if (!context.isEmpty)
			{
				// ��ʼ�������е��ӱ�
				TABLE_DATAS_MAP::iterator tableIter = childTableDatas.begin();
				for (; tableIter != childTableDatas.end(); ++tableIter)
				{
					TABLE_DATAS& datas = tableIter->second;
					KBEUnordered_map<std::string, std::vector<DBID> >::iterator iter =
						childTableDBIDs.find(tableIter->first);

					if (iter != childTableDBIDs.end())
					{
						int nBatchlyWrite = writeDBBatchly(optype, pdbi, context, tableIter->first, datas, iter->second);
						if (nBatchlyWrite >= 0)
						{
							iter->second.erase(iter->second.begin(), iter->second.begin() + nBatchlyWrite);
							for (size_t i = nBatchlyWrite;i < datas.size(); ++i)
							{
								mysql::DBContext& childContext = *(datas[i].get());
								childContext.parentTableDBID = context.dbid;
								childContext.dbid = 0;
								writeDB(optype, pdbi, childContext);
							}
						}
					}
				}

				/*mysql::DBContext::DB_RW_CONTEXTS::iterator iter1 = context.optable.begin();
				for(; iter1 != context.optable.end(); ++iter1)
				{
					mysql::DBContext& wbox = *iter1->second.get();

					if(wbox.isEmpty)
						continue;

					// �󶨱��ϵ
					wbox.parentTableDBID = context.dbid;

					KBEUnordered_map<std::string, std::vector<DBID> >::iterator iter =
						childTableDBIDs.find(wbox.tableName);

					if(iter != childTableDBIDs.end())
					{
						if(iter->second.size() > 0)
						{
							wbox.dbid = iter->second.front();
							iter->second.erase(iter->second.begin());
						}

						if(iter->second.size() <= 0)
						{
							childTableDBIDs.erase(wbox.tableName);
						}
					}

					// �����ӱ�
					writeDB(optype, pdbi, wbox);
				}*/
			}

			// ɾ��������������
			KBEUnordered_map< std::string, std::vector<DBID> >::iterator tabiter = childTableDBIDs.begin();
			for (; tabiter != childTableDBIDs.end(); ++tabiter)
			{
				if (tabiter->second.size() == 0)
					continue;

				// ��ɾ�����ݿ��еļ�¼
				std::string sqlstr = "delete from " ENTITY_TABLE_PERFIX "_";
				sqlstr += tabiter->first;
				sqlstr += " where " TABLE_ID_CONST_STR " in (";

				std::vector<DBID>::iterator iter = tabiter->second.begin();
				for (; iter != tabiter->second.end(); ++iter)
				{
					DBID dbid = (*iter);

					char sqlstr1[MAX_BUF];
					kbe_snprintf(sqlstr1, MAX_BUF, "%" PRDBID, dbid);
					sqlstr += sqlstr1;
					sqlstr += ",";
				}

				sqlstr.erase(sqlstr.size() - 1);
				sqlstr += ")";
				bool ret = pdbi->query(sqlstr.c_str(), sqlstr.size(), false);
				KBE_ASSERT(ret);

				TABLE_DATAS_MAP::iterator it = childTableDatas.find(tabiter->first);
				if (it != childTableDatas.end() && !it->second.empty())
				{
					mysql::DBContext& wbox = *it->second[0].get();
					for (; iter != tabiter->second.end(); ++iter)
					{
						DBID dbid = (*iter);

						wbox.parentTableDBID = context.dbid;
						wbox.dbid = dbid;
						wbox.isEmpty = true;

						// ɾ���ӱ�
						writeDB(optype, pdbi, wbox);
					}
				}
			}
		}
		return ret;
	}

protected:

};

}
#endif // KBE_WRITE_ENTITY_HELPER_H

