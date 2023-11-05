/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2023/06/25.
//

#include "net/plain_communicator.h"
#include "net/buffered_writer.h"
#include "sql/expr/tuple.h"
#include "event/session_event.h"
#include "session/session.h"
#include "common/io/io.h"
#include "common/log/log.h"

PlainCommunicator::PlainCommunicator()
{
  send_message_delimiter_.assign(1, '\0');
  debug_message_prefix_.resize(2);
  debug_message_prefix_[0] = '#';
  debug_message_prefix_[1] = ' ';
}

RC PlainCommunicator::read_event(SessionEvent *&event)
{
  RC rc = RC::SUCCESS;

  event = nullptr;

  int data_len = 0;
  int read_len = 0;

  const int max_packet_size = 8192;
  std::vector<char> buf(max_packet_size);

  // 持续接收消息，直到遇到'\0'。将'\0'遇到的后续数据直接丢弃没有处理，因为目前仅支持一收一发的模式
  while (true) {
    read_len = ::read(fd_, buf.data() + data_len, max_packet_size - data_len);
    if (read_len < 0) {
      if (errno == EAGAIN) {
        continue;
      }
      break;
    }
    if (read_len == 0) {
      break;
    }

    if (read_len + data_len > max_packet_size) {
      data_len += read_len;
      break;
    }

    bool msg_end = false;
    for (int i = 0; i < read_len; i++) {
      if (buf[data_len + i] == 0) {
        data_len += i + 1;
        msg_end = true;
        break;
      }
    }

    if (msg_end) {
      break;
    }

    data_len += read_len;
  }

  if (data_len > max_packet_size) {
    LOG_WARN("The length of sql exceeds the limitation %d", max_packet_size);
    return RC::IOERR_TOO_LONG;
  }
  if (read_len == 0) {
    LOG_INFO("The peer has been closed %s", addr());
    return RC::IOERR_CLOSE;
  } else if (read_len < 0) {
    LOG_ERROR("Failed to read socket of %s, %s", addr(), strerror(errno));
    return RC::IOERR_READ;
  }

  LOG_INFO("receive command(size=%d): %s", data_len, buf.data());
  event = new SessionEvent(this);
  event->set_query(std::string(buf.data()));
  return rc;
}

RC PlainCommunicator::write_state(SessionEvent *event, bool &need_disconnect)
{
  SqlResult *sql_result = event->sql_result();
  const int buf_size = 2048;
  char *buf = new char[buf_size];
  const std::string &state_string = sql_result->state_string();
  if (state_string.empty()) {
    const char *result = RC::SUCCESS == sql_result->return_code() ? "SUCCESS" : "FAILURE";
    snprintf(buf, buf_size, "%s\n", result);
  } else {
    snprintf(buf, buf_size, "%s > %s\n", strrc(sql_result->return_code()), state_string.c_str());
  }

  RC rc = writer_->writen(buf, strlen(buf));
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to send data to client. err=%s", strerror(errno));
    need_disconnect = true;
    delete[] buf;
    return RC::IOERR_WRITE;
  }

  need_disconnect = false;
  delete[] buf;

  return RC::SUCCESS;
}

RC PlainCommunicator::write_debug(SessionEvent *request, bool &need_disconnect)
{
  if (!session_->sql_debug_on()) {
    return RC::SUCCESS;
  }

  SqlDebug &sql_debug = request->sql_debug();
  const std::list<std::string> &debug_infos = sql_debug.get_debug_infos();
  for (auto &debug_info : debug_infos) {
    RC rc = writer_->writen(debug_message_prefix_.data(), debug_message_prefix_.size());
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send data to client. err=%s", strerror(errno));
      need_disconnect = true;
      return RC::IOERR_WRITE;
    }

    rc = writer_->writen(debug_info.data(), debug_info.size());
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send data to client. err=%s", strerror(errno));
      need_disconnect = true;
      return RC::IOERR_WRITE;
    }

    char newline = '\n';
    rc = writer_->writen(&newline, 1);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send new line to client. err=%s", strerror(errno));
      need_disconnect = true;
      return RC::IOERR_WRITE;
    }
  }

  need_disconnect = false;
  return RC::SUCCESS;
}

RC PlainCommunicator::write_result(SessionEvent *event, bool &need_disconnect)
{
  RC rc = write_result_internal(event, need_disconnect);
  if (!need_disconnect) {
    RC rc1 = write_debug(event, need_disconnect);
    if (OB_FAIL(rc1)) {
      LOG_WARN("failed to send debug info to client. rc=%s, err=%s", strrc(rc), strerror(errno));
    }
  }
  if (!need_disconnect) {
    rc = writer_->writen(send_message_delimiter_.data(), send_message_delimiter_.size());
    if (OB_FAIL(rc)) {
      LOG_ERROR("Failed to send data back to client. ret=%s, error=%s", strrc(rc), strerror(errno));
      need_disconnect = true;
      return rc;
    }
  }
  writer_->flush(); // TODO handle error
  return rc;
}

RC PlainCommunicator::write_result_internal(SessionEvent *event, bool &need_disconnect)
{
  RC rc = RC::SUCCESS;
  need_disconnect = true;

  SqlResult *sql_result = event->sql_result();

  if (RC::SUCCESS != sql_result->return_code() || !sql_result->has_operator()) {
    return write_state(event, need_disconnect);
  }

  rc = sql_result->open();
  if (OB_FAIL(rc)) {
    sql_result->close();
    sql_result->set_return_code(rc);
    return write_state(event, need_disconnect);
  }

  const TupleSchema &schema = sql_result->tuple_schema();
  int cell_num = schema.cell_num();

  // 增加bitmap列后，如果是联表查询，那么结果会出现多个bitmap列
  // 存储每个bitmap的索引，后续投影时忽略
  std::vector<int> ignored_index;

  for (int i = 0; i < cell_num; i++) {
    const TupleCellSpec &spec = schema.cell_at(i);
    const char *alias = spec.alias();
    // std::cout  << spec.table_name() << " & " << spec.field_name() << " & " << spec.alias() << std::endl;

    if (strcmp(alias, NULL_FIELD_NAME.c_str()) == 0 || strcmp(spec.field_name(), NULL_FIELD_NAME.c_str()) == 0){
      ignored_index.push_back(i);
    }


    bool ffflag = false;
    if(ignored_index.size() != 0 && ignored_index[ignored_index.size() - 1] == i){
      ffflag = true;
    }
    if (!ffflag && (nullptr != alias || alias[0] != 0)) {
      if (0 != i) {
        const char *delim = " | ";
        rc = writer_->writen(delim, strlen(delim));
        if (OB_FAIL(rc)) {
          LOG_WARN("failed to send data to client. err=%s", strerror(errno));
          return rc;
        }
      }

      int len = strlen(alias);

      rc = writer_->writen(alias, len);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to send data to client. err=%s", strerror(errno));
        sql_result->close();
        return rc;
      }
    }
  }

  if (cell_num > 0) {
    char newline = '\n';
    rc = writer_->writen(&newline, 1);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send data to client. err=%s", strerror(errno));
      sql_result->close();
      return rc;
    }
  }



  rc = RC::SUCCESS;
  Tuple *tuple = nullptr;

  auto order_rules = sql_result->order_rules;
  /* 为了实现order by， 不得已只能在这里修改了  */

  if (order_rules.size() > 0)
  {
    // 获得排序列的索引与标识
    std::vector<int> order_index;
    std::vector<OrderOp> order_op;

    for(std::pair<RelAttrSqlNode, OrderOp> it: order_rules) 
    {
      order_op.push_back(it.second);
      for(int i = 0; i < cell_num; i++){
        const TupleCellSpec &spec = schema.cell_at(i);
        if(  (strlen(spec.table_name()) == 0 && strcmp(spec.alias(), it.first.attribute_name.c_str()) == 0 )
          || ((strcmp(spec.table_name(), it.first.relation_name.c_str()) == 0) && strcmp(spec.field_name(), it.first.attribute_name.c_str()) == 0)
        )
        {
          order_index.push_back(i);
          break;
        }
      }
    }

    // 取出全部Tuple
    std::vector<std::vector<Value>> tuple_set;
    while (RC::SUCCESS == (rc = sql_result->next_tuple(tuple))) {
      std::vector<Value> temp;
      int num_cell = tuple->cell_num();
      for(int i = 0; i < num_cell; i++){
        Value cell;
        tuple->cell_at(i, cell);
        temp.push_back(cell);
      }
      tuple_set.push_back(temp);    
    }

    // 排序
    std::sort(tuple_set.begin(), tuple_set.end(), 
      [order_index, order_op](std::vector<Value>& t1, std::vector<Value>& t2){
        for (int i = 0; i < order_index.size(); i++)
        {
          int target_index = order_index[i];
          Value v1 = t1[target_index];
          Value v2 = t2[target_index];
          int ret = v1.compare(v2);
          if (ret != 0)
          {
            return (order_op[i] == ORDER_ASC || order_op[i] == ORDER_DEFAULT) ? ret <= 0 : ret >= 0;
          }
        }
        return true;
      }
    );

    // 输出
    for(int i = 0; i < tuple_set.size(); i++){
      for(int j = 0; j < tuple_set[i].size(); j++)
      {

        // 忽略bitmap列
        bool need_ignore = false;
        for (int t = 0; t < ignored_index.size(); t++) {
          if (j == ignored_index[t]) {
            need_ignore = true;
            break;
          }
        }
        if(need_ignore)
          continue;


        if (j != 0) {
          const char *delim = " | ";
          rc = writer_->writen(delim, strlen(delim));
          if (OB_FAIL(rc)) {
            LOG_WARN("failed to send data to client. err=%s", strerror(errno));
            sql_result->close();
            return rc;
          }
        }

        std::string cell_str = tuple_set[i][j].to_string();
        rc = writer_->writen(cell_str.data(), cell_str.size());
        if (OB_FAIL(rc)) {
          LOG_WARN("failed to send data to client. err=%s", strerror(errno));
          sql_result->close();
          return rc;
        }
      }

      char newline = '\n';
      rc = writer_->writen(&newline, 1);
      if (OB_FAIL(rc)) 
      {
        LOG_WARN("failed to send data to client. err=%s", strerror(errno));
        sql_result->close();
        return rc;
      }
    }
  }
  else {

    while (RC::SUCCESS == (rc = sql_result->next_tuple(tuple))) {
      assert(tuple != nullptr);

      int cell_num = tuple->cell_num();
      for (int i = 0; i < cell_num; i++) {
        bool need_ignore = false;
        for (int t = 0; t < ignored_index.size(); t++) {
          if (ignored_index[t] == i) {
            need_ignore = true;
            break;
          }
        }
        if(need_ignore)
          continue;
        if (i != 0) {
          const char *delim = " | ";
          rc = writer_->writen(delim, strlen(delim));
          if (OB_FAIL(rc)) {
            LOG_WARN("failed to send data to client. err=%s", strerror(errno));
            sql_result->close();
            return rc;
          }
        }

        Value value;
        rc = tuple->cell_at(i, value);
        if (rc != RC::SUCCESS) {
          sql_result->close();
          return rc;
        }

        std::string cell_str = value.to_string();
        rc = writer_->writen(cell_str.data(), cell_str.size());
        if (OB_FAIL(rc)) {
          LOG_WARN("failed to send data to client. err=%s", strerror(errno));
          sql_result->close();
          return rc;
        }
      }

      char newline = '\n';
      rc = writer_->writen(&newline, 1);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to send data to client. err=%s", strerror(errno));
        sql_result->close();
        return rc;
      }
    }

  }

  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }

  if (cell_num == 0) {
    // 除了select之外，其它的消息通常不会通过operator来返回结果，表头和行数据都是空的
    // 这里针对这种情况做特殊处理，当表头和行数据都是空的时候，就返回处理的结果
    // 可能是insert/delete等操作，不直接返回给客户端数据，这里把处理结果返回给客户端
    RC rc_close = sql_result->close();
    if (rc == RC::SUCCESS) {
      rc = rc_close;
    }
    sql_result->set_return_code(rc);
    return write_state(event, need_disconnect);
  } else {
    need_disconnect = false;
  }

  RC rc_close = sql_result->close();
  if (OB_SUCC(rc)) {
    rc = rc_close;
  }

  return rc;
}