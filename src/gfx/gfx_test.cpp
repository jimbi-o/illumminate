#include "render_graph.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "minimal_for_cpp.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
TEST_CASE("batch-pass") {
  using namespace illuminate::gfx;
  {
    RenderGraphConfig<uint32_t> config;
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    CHECK(batch_order.empty());
    CHECK(batched_pass_order.empty());
    CHECK(pass_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {{}, 1, CommandQueueType::kGraphics},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    CHECK(batch_order.size() == 1);
    CHECK(batched_pass_order.size() == 1);
    CHECK(batched_pass_order[0].size() == 1);
    CHECK(batched_pass_order[0][0] == 0);
    CHECK(pass_list.size() == 1);
    CHECK(pass_list[0].render_function == 1);
    CHECK(pass_list[0].command_queue_type == CommandQueueType::kGraphics);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {{}, 1, CommandQueueType::kGraphics, },
          {{}, 2, CommandQueueType::kCompute},
        }},
        {{
          {{}, 4, CommandQueueType::kCompute},
          {{}, 3, CommandQueueType::kGraphics},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    CHECK(batch_order.size() == 2);
    CHECK(batched_pass_order.size() == 2);
    CHECK(batched_pass_order[0].size() == 2);
    CHECK(batched_pass_order[0][0] == 0);
    CHECK(batched_pass_order[0][1] == 1);
    CHECK(batched_pass_order[1][0] == 2);
    CHECK(batched_pass_order[1][1] == 3);
    CHECK(pass_list.size() == 4);
    CHECK(pass_list[0].render_function == 1);
    CHECK(pass_list[1].render_function == 2);
    CHECK(pass_list[2].render_function == 4);
    CHECK(pass_list[3].render_function == 3);
    CHECK(pass_list[0].command_queue_type == CommandQueueType::kGraphics);
    CHECK(pass_list[1].command_queue_type == CommandQueueType::kCompute);
    CHECK(pass_list[2].command_queue_type == CommandQueueType::kCompute);
    CHECK(pass_list[3].command_queue_type == CommandQueueType::kGraphics);
  }
}
#if 0
// TODO use resource id
TEST_CASE("validate render graph config") {
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("subbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 1);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
    CHECK(batched_pass_order.at(1)[1] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(1).size() == 1);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
    CHECK(batched_pass_order.at(1)[1] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
    CHECK(batched_pass_order.at(1)[1] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("subbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 3);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(1)[0] == 3);
    CHECK(batched_pass_order.at(1)[1] == 4);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 4);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 1);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 3);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 2);
    CHECK(batch_order[2] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(2).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(2)[0] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
    CHECK(batched_pass_order.at(1)[1] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyA")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("subbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }},
        {{
            {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyC")}}, {ResourceStateType::kUavReadWrite, {StrId("subbuffer")}}}},
            {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyB")}}}},
            {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("subbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 3);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 2);
    CHECK(batch_order[2] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(2).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 3);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(2)[0] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
    CHECK(batched_pass_order.at(1)[1] == 3);
    CHECK(batched_pass_order.at(1)[2] == 4);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 4);
    CHECK(batched_pass_order.at(1).size() == 3);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(1)[0] == 4);
    CHECK(batched_pass_order.at(1)[1] == 5);
    CHECK(batched_pass_order.at(1)[2] == 6);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 7);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(0)[4] == 4);
    CHECK(batched_pass_order.at(0)[5] == 5);
    CHECK(batched_pass_order.at(0)[6] == 6);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batched_pass_order.at(0).size() == 8);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(0)[4] == 4);
    CHECK(batched_pass_order.at(0)[5] == 5);
    CHECK(batched_pass_order.at(0)[6] == 6);
    CHECK(batched_pass_order.at(0)[7] == 7);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer"), StrId("dmyF")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batched_pass_order.at(0).size() == 10);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(0)[4] == 4);
    CHECK(batched_pass_order.at(0)[5] == 5);
    CHECK(batched_pass_order.at(0)[6] == 6);
    CHECK(batched_pass_order.at(0)[7] == 7);
    CHECK(batched_pass_order.at(0)[8] == 8);
    CHECK(batched_pass_order.at(0)[9] == 9);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("cbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("dbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("subbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}, {ResourceStateType::kUavReadWrite, {StrId("cbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 4);
    CHECK(batched_pass_order.at(1).size() == 4);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(1)[0] == 4);
    CHECK(batched_pass_order.at(1)[1] == 5);
    CHECK(batched_pass_order.at(1)[2] == 6);
    CHECK(batched_pass_order.at(1)[3] == 7);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("cbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("dbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("subbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}, {ResourceStateType::kUavReadWrite, {StrId("cbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("cbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("dbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
        }},
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    SUBCASE("sequential id") {
      std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
      CHECK(batch_order.size() == 4);
      CHECK(batch_order[0] == 0);
      CHECK(batch_order[1] == 3);
      CHECK(batch_order[2] == 1);
      CHECK(batch_order[3] == 2);
      CHECK(batched_pass_order.at(0).size() == 4);
      CHECK(batched_pass_order.at(3).size() == 4);
      CHECK(batched_pass_order.at(1).size() == 4);
      CHECK(batched_pass_order.at(2).size() == 3);
      CHECK(batched_pass_order.at(0)[0] == 0);
      CHECK(batched_pass_order.at(0)[1] == 1);
      CHECK(batched_pass_order.at(0)[2] == 2);
      CHECK(batched_pass_order.at(0)[3] == 3);
      CHECK(batched_pass_order.at(3)[0] == 4);
      CHECK(batched_pass_order.at(3)[1] == 5);
      CHECK(batched_pass_order.at(3)[2] == 6);
      CHECK(batched_pass_order.at(3)[3] == 7);
      CHECK(batched_pass_order.at(1)[0] == 8);
      CHECK(batched_pass_order.at(1)[1] == 9);
      CHECK(batched_pass_order.at(1)[2] == 10);
      CHECK(batched_pass_order.at(1)[3] == 11);
      CHECK(batched_pass_order.at(2)[0] == 12);
      CHECK(batched_pass_order.at(2)[1] == 13);
      CHECK(batched_pass_order.at(2)[2] == 14);
    }
    SUBCASE("non-sequential id") {
      batch_order.push_back(10);
      batch_order.push_back(9);
      batched_pass_order[10] = {};
      batched_pass_order[9] = {};
      std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
      CHECK(batch_order.size() == 6);
      CHECK(batch_order[0] == 0);
      CHECK(batch_order[1] == 11);
      CHECK(batch_order[2] == 1);
      CHECK(batch_order[3] == 2);
      CHECK(batch_order[4] == 10);
      CHECK(batch_order[5] == 9);
      CHECK(batched_pass_order.at(0).size() == 4);
      CHECK(batched_pass_order.at(11).size() == 4);
      CHECK(batched_pass_order.at(1).size() == 4);
      CHECK(batched_pass_order.at(2).size() == 3);
      CHECK(batched_pass_order.at(9).empty());
      CHECK(batched_pass_order.at(10).empty());
      CHECK(batched_pass_order.at(0)[0] == 0);
      CHECK(batched_pass_order.at(0)[1] == 1);
      CHECK(batched_pass_order.at(0)[2] == 2);
      CHECK(batched_pass_order.at(0)[3] == 3);
      CHECK(batched_pass_order.at(11)[0] == 4);
      CHECK(batched_pass_order.at(11)[1] == 5);
      CHECK(batched_pass_order.at(11)[2] == 6);
      CHECK(batched_pass_order.at(11)[3] == 7);
      CHECK(batched_pass_order.at(1)[0] == 8);
      CHECK(batched_pass_order.at(1)[1] == 9);
      CHECK(batched_pass_order.at(1)[2] == 10);
      CHECK(batched_pass_order.at(1)[3] == 11);
      CHECK(batched_pass_order.at(2)[0] == 12);
      CHECK(batched_pass_order.at(2)[1] == 13);
      CHECK(batched_pass_order.at(2)[2] == 14);
    }
  }
}
#endif
TEST_CASE("set id to resources") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  {
    RenderGraphConfig<uint32_t> config;
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto resource_id_list = IdentifyPassResources(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(resource_id_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {{{ResourceStateType::kRtvNewBuffer, {StrId("A"), StrId("C")}}}, 1, CommandQueueType::kGraphics, },
          {{{ResourceStateType::kUavNewBuffer, {StrId("B"), StrId("D")}}}, 1, CommandQueueType::kGraphics, },
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto resource_id_list = IdentifyPassResources(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(resource_id_list.size() == 2);
    CHECK(resource_id_list.at(0).size() == 1);
    CHECK(resource_id_list.at(0).contains(ResourceStateType::kRtvNewBuffer));
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer).size() == 2);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[0] == 0);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[1] == 1);
    CHECK(resource_id_list.at(1).size() == 1);
    CHECK(resource_id_list.at(1).contains(ResourceStateType::kUavNewBuffer));
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer).size() == 2);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer)[0] == 2);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer)[1] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {{{ResourceStateType::kRtvNewBuffer, {StrId("A"), StrId("B")}}}, 1, CommandQueueType::kGraphics, },
          {{{ResourceStateType::kUavNewBuffer, {StrId("A"), StrId("B")}}}, 1, CommandQueueType::kGraphics, },
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto resource_id_list = IdentifyPassResources(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(resource_id_list.size() == 2);
    CHECK(resource_id_list.at(0).size() == 1);
    CHECK(resource_id_list.at(0).contains(ResourceStateType::kRtvNewBuffer));
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer).size() == 2);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[0] == 0);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[1] == 1);
    CHECK(resource_id_list.at(1).size() == 1);
    CHECK(resource_id_list.at(1).contains(ResourceStateType::kUavNewBuffer));
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer).size() == 2);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer)[0] == 2);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer)[1] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {{{ResourceStateType::kRtvNewBuffer, {StrId("A"), StrId("B")}}}, 1, CommandQueueType::kGraphics, },
          {{{ResourceStateType::kRtvPrevResultReused, {StrId("A"), StrId("B")}}}, 1, CommandQueueType::kGraphics, },
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto resource_id_list = IdentifyPassResources(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(resource_id_list.size() == 2);
    CHECK(resource_id_list.at(0).size() == 1);
    CHECK(resource_id_list.at(0).contains(ResourceStateType::kRtvNewBuffer));
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer).size() == 2);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[0] == 0);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[1] == 1);
    CHECK(resource_id_list.at(1).size() == 1);
    CHECK(resource_id_list.at(1).contains(ResourceStateType::kRtvPrevResultReused));
    CHECK(resource_id_list.at(1).at(ResourceStateType::kRtvPrevResultReused).size() == 2);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kRtvPrevResultReused)[0] == 0);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kRtvPrevResultReused)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          // g-buffer
          {{{ResourceStateType::kRtvNewBuffer, {StrId("gbuf0-f1"), StrId("gbuf1-f1"), StrId("gbuf2-f1")}}, {ResourceStateType::kDsvReadOnly, {StrId("depth-f1")}}}, 1, CommandQueueType::kGraphics,},
          // linear depth
          {{{ResourceStateType::kSrv, {StrId("depth-f1")}}, {ResourceStateType::kUavNewBuffer, {StrId("lineardepth-f1")}}}, 1, CommandQueueType::kCompute, AsyncComputeAllowed::kAllowed},
          // light clustering
          {{{ResourceStateType::kSrv, {StrId("lineardepth-f1")}}, {ResourceStateType::kUavNewBuffer, {StrId("lightcluster-f1")}}}, 1, CommandQueueType::kCompute, AsyncComputeAllowed::kAllowed},
        }},
        {{
          // pre-z (next frame)
          {{{ResourceStateType::kDsvNewBuffer, {StrId("depth-f0")}}}, 1, CommandQueueType::kGraphics,},
          // shadow map (next frame)
          {{{ResourceStateType::kDsvNewBuffer, {StrId("shadowmap-f0")}}}, 1, CommandQueueType::kGraphics,},
          // deferred shadow
          {{{ResourceStateType::kSrv, {StrId("shadowmap-f1")}}, {ResourceStateType::kUavNewBuffer, {StrId("shadowtex-f1")}}}, 1, CommandQueueType::kCompute, AsyncComputeAllowed::kAllowed},
          // lighting
          {{{ResourceStateType::kSrv, {StrId("gbuf0-f1"), StrId("gbuf1-f1"), StrId("gbuf2-f1"), StrId("lineardepth-f1"), StrId("shadowtex-f1"), StrId("lightcluster-f1")}}, {ResourceStateType::kUavNewBuffer, {StrId("mainbuffer-f1")}}}, 1, CommandQueueType::kCompute, AsyncComputeAllowed::kAllowed},
          // transparent geometry
          {{{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer-f1")}}, {ResourceStateType::kDsvReadOnly, {StrId("depth-f1")}}}, 1, CommandQueueType::kGraphics,},
          // post process
          {{{ResourceStateType::kSrv, {StrId("mainbuffer-f1")}}, {ResourceStateType::kUavNewBuffer, {StrId("mainbuffer-f1")}}}, 1, CommandQueueType::kCompute, AsyncComputeAllowed::kAllowed},
        }},
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto resource_id_list = IdentifyPassResources(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(resource_id_list.size() == 9);
    // g-buffer
    CHECK(resource_id_list.at(0).size() == 2);
    CHECK(resource_id_list.at(0).contains(ResourceStateType::kRtvNewBuffer));
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer).size() == 3);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[0] + 1 == resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[1]);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[1] + 1 == resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[2]);
    CHECK(resource_id_list.at(0).contains(ResourceStateType::kDsvReadOnly));
    CHECK(resource_id_list.at(0).at(ResourceStateType::kDsvReadOnly).size() == 1);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kDsvReadOnly)[0] != resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[0]);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kDsvReadOnly)[0] != resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[1]);
    CHECK(resource_id_list.at(0).at(ResourceStateType::kDsvReadOnly)[0] != resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[2]);
    // linear depth
    CHECK(resource_id_list.at(1).size() == 2);
    CHECK(resource_id_list.at(1).contains(ResourceStateType::kSrv));
    CHECK(resource_id_list.at(1).at(ResourceStateType::kSrv).size() == 1);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kSrv)[0] > resource_id_list.at(2).at(ResourceStateType::kDsvNewBuffer)[0]);
    CHECK(resource_id_list.at(1).contains(ResourceStateType::kUavNewBuffer));
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer).size() == 1);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer)[0] > resource_id_list.at(2).at(ResourceStateType::kDsvNewBuffer)[0]);
    CHECK(resource_id_list.at(1).at(ResourceStateType::kSrv)[0] != resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer)[0]);
    // light clustering
    CHECK(resource_id_list.at(2).size() == 2);
    CHECK(resource_id_list.at(2).contains(ResourceStateType::kSrv));
    CHECK(resource_id_list.at(2).at(ResourceStateType::kSrv).size() == 1);
    CHECK(resource_id_list.at(2).at(ResourceStateType::kSrv)[0] == resource_id_list.at(1).at(ResourceStateType::kUavNewBuffer)[0]);
    CHECK(resource_id_list.at(2).contains(ResourceStateType::kUavNewBuffer));
    CHECK(resource_id_list.at(2).at(ResourceStateType::kUavNewBuffer).size() == 1);
    CHECK(resource_id_list.at(2).at(ResourceStateType::kUavNewBuffer)[0] > resource_id_list.at(2).at(ResourceStateType::kDsvNewBuffer)[0]);
    CHECK(resource_id_list.at(2).at(ResourceStateType::kSrv)[0] != resource_id_list.at(2).at(ResourceStateType::kUavNewBuffer)[0]);
    // pre-z
    CHECK(resource_id_list.at(3).size() == 1);
    CHECK(resource_id_list.at(3).contains(ResourceStateType::kDsvNewBuffer));
    CHECK(resource_id_list.at(3).at(ResourceStateType::kDsvNewBuffer).size() == 1);
    CHECK(resource_id_list.at(3).at(ResourceStateType::kDsvNewBuffer)[0] == 4);
    // shadow map
    CHECK(resource_id_list.at(4).size() == 1);
    CHECK(resource_id_list.at(4).contains(ResourceStateType::kDsvNewBuffer));
    CHECK(resource_id_list.at(4).at(ResourceStateType::kDsvNewBuffer).size() == 1);
    CHECK(resource_id_list.at(4).at(ResourceStateType::kDsvNewBuffer)[0] == 5);
    // deferred shadow
    CHECK(resource_id_list.at(5).size() == 2);
    CHECK(resource_id_list.at(5).contains(ResourceStateType::kSrv));
    CHECK(resource_id_list.at(5).at(ResourceStateType::kSrv).size() == 1);
    CHECK(resource_id_list.at(5).at(ResourceStateType::kSrv)[0] > resource_id_list.at(3).at(ResourceStateType::kDsvNewBuffer)[0]);
    CHECK(resource_id_list.at(5).at(ResourceStateType::kSrv)[0] > resource_id_list.at(3).at(ResourceStateType::kUavNewBuffer)[0]);
    CHECK(resource_id_list.at(5).contains(ResourceStateType::kUavNewBuffer));
    CHECK(resource_id_list.at(5).at(ResourceStateType::kUavNewBuffer).size() == 1);
    CHECK(resource_id_list.at(5).at(ResourceStateType::kUavNewBuffer)[0] > resource_id_list.at(3).at(ResourceStateType::kDsvNewBuffer)[0]);
    CHECK(resource_id_list.at(5).at(ResourceStateType::kUavNewBuffer)[0] > resource_id_list.at(3).at(ResourceStateType::kUavNewBuffer)[0]);
    CHECK(resource_id_list.at(5).at(ResourceStateType::kSrv)[0] != resource_id_list.at(5).at(ResourceStateType::kUavNewBuffer)[0]);
    // lighting
    CHECK(resource_id_list.at(6).size() == 2);
    CHECK(resource_id_list.at(6).contains(ResourceStateType::kSrv));
    CHECK(resource_id_list.at(6).at(ResourceStateType::kSrv).size() == 6);
    CHECK(resource_id_list.at(6).at(ResourceStateType::kSrv)[0] == resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[0]);
    CHECK(resource_id_list.at(6).at(ResourceStateType::kSrv)[1] == resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[1]);
    CHECK(resource_id_list.at(6).at(ResourceStateType::kSrv)[2] == resource_id_list.at(0).at(ResourceStateType::kRtvNewBuffer)[2]);
    CHECK(resource_id_list.at(6).at(ResourceStateType::kSrv)[3] == resource_id_list.at(0).at(ResourceStateType::kDsvReadOnly)[0]);
    CHECK(resource_id_list.at(6).at(ResourceStateType::kSrv)[4] == resource_id_list.at(5).at(ResourceStateType::kUavNewBuffer)[0]);
    CHECK(resource_id_list.at(6).at(ResourceStateType::kSrv)[5] == resource_id_list.at(2).at(ResourceStateType::kUavNewBuffer)[0]);
    CHECK(resource_id_list.at(6).contains(ResourceStateType::kUavNewBuffer));
    CHECK(resource_id_list.at(6).at(ResourceStateType::kUavNewBuffer).size() == 1);
    CHECK(resource_id_list.at(6).at(ResourceStateType::kUavNewBuffer)[0] == resource_id_list.at(5).at(ResourceStateType::kUavNewBuffer)[0] + 1);
    // transparent geometry
    CHECK(resource_id_list.at(7).size() == 2);
    CHECK(resource_id_list.at(7).contains(ResourceStateType::kRtvNewBuffer));
    CHECK(resource_id_list.at(7).at(ResourceStateType::kRtvNewBuffer).size() == 1);
    CHECK(resource_id_list.at(7).at(ResourceStateType::kRtvNewBuffer)[0] == resource_id_list.at(6).at(ResourceStateType::kUavNewBuffer)[0] + 1);
    CHECK(resource_id_list.at(7).contains(ResourceStateType::kDsvReadOnly));
    CHECK(resource_id_list.at(7).at(ResourceStateType::kDsvReadOnly).size() == 1);
    CHECK(resource_id_list.at(7).at(ResourceStateType::kDsvReadOnly)[0] == resource_id_list.at(0).at(ResourceStateType::kDsvReadOnly)[0]);
    // post process
    CHECK(resource_id_list.at(8).size() == 2);
    CHECK(resource_id_list.at(8).contains(ResourceStateType::kSrv));
    CHECK(resource_id_list.at(8).at(ResourceStateType::kSrv).size() == 1);
    CHECK(resource_id_list.at(8).at(ResourceStateType::kSrv)[0] == resource_id_list.at(7).at(ResourceStateType::kRtvNewBuffer)[0]);
    CHECK(resource_id_list.at(8).contains(ResourceStateType::kUavNewBuffer));
    CHECK(resource_id_list.at(8).at(ResourceStateType::kUavNewBuffer).size() == 1);
    CHECK(resource_id_list.at(8).at(ResourceStateType::kUavNewBuffer)[0] == resource_id_list.at(7).at(ResourceStateType::kRtvNewBuffer)[0] + 1);
  }
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
