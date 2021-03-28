using GoHireNow.Api.Filters;
using GoHireNow.Database;
using GoHireNow.Identity.Data;
using GoHireNow.Models.CommonModels;
using GoHireNow.Models.CommonModels.Enums;
using GoHireNow.Models.StripeModels;
using GoHireNow.Service.Interfaces;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Identity;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Configuration;
using Stripe;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading.Tasks;

namespace GoHireNow.Api.Controllers
{
    //[Authorize(Roles = "Client")]
    [Route("payment")]
    [ApiController]
    [CustomExceptionFilter]
    public class PaymentController : BaseController
    {
        private readonly UserManager<ApplicationUser> _userManager;
        private readonly IStripePaymentService _stripePaymentService;
        private readonly IPlanService _planService;
        private readonly ICustomLogService _customLogService;
        private readonly IClientService _clientService;
        private IConfiguration _configuration { get; }
        public PaymentController(UserManager<ApplicationUser> userManager,
            IConfiguration configuration,
            IStripePaymentService stripePaymentService, IClientService clientService,
            IPlanService planService,
            ICustomLogService customLogService)
        {
            _userManager = userManager;
            _configuration = configuration;
            _stripePaymentService = stripePaymentService;
            _planService = planService;
            _customLogService = customLogService;
            _clientService = clientService;
            //StripeConfiguration.ApiKey = "sk_test_Afm3ud6IM3Qvd1sAPoqoqjhu00SaLjZ41T";
        }

        /// <summary>
        /// Called when payment is made on stripe and user is back on front-end site
        /// </summary>
        /// <param name="model"></param>
        /// <returns></returns>
        [HttpPost]
        [Route("charge")]
        public async Task<StripeApiResponse> Charge([FromForm]PaymentChargeRequest model)
        {
            LogErrorRequest error;
            var response = new StripeApiResponse();
            try
            {
                var user = await _userManager.FindByIdAsync(UserId);
                var customerId = user.CustomerStripeId;
                var freeplan = await _stripePaymentService.GetGlobalPlanDetail((int)GlobalPlanEnum.Free);
                if (freeplan.AccessId == model.planId)
                {
                    if (!string.IsNullOrEmpty(user.CustomerStripeId))
                    {
                        CancelSubscription(user.CustomerStripeId);
                    }
                    response.StatusCode = HttpStatusCode.OK;
                    return response;
                }
                //Create Token
                //var options = new TokenCreateOptions
                //{
                //    Card = new CreditCardOptions
                //    {
                //        Number = "4242424242424242",
                //        ExpYear = 2020,
                //        ExpMonth = 11,
                //        Cvc = "123"
                //    }
                //};
                //Create Token END
                var customers = new CustomerService();
                var subscriptions = new SubscriptionService();
                if (user == null)
                {
                    //var customer = customers.Create(new CustomerCreateOptions
                    //{
                    //    Name = user.Company,
                    //    Email = user.Email,
                    //    Source = stripeToken.Id
                    //});
                    //var customerId = customer.Id;
                    //var plan = new PlanService().Get(model.planId);
                    //var charge = charges.Create(new ChargeCreateOptions
                    //{
                    //    Amount = plan.Amount,
                    //    Description = new ProductService().Get(plan.ProductId).Description,
                    //    Currency = "usd",
                    //    CustomerId = customerId
                    //});

                    //CreateCustomerSubscription(subscriptions, customerId, plan);

                    //user = await _userManager.FindByIdAsync(UserId);
                    //user.CustomerStripeId = customerId;
                    //await _userManager.UpdateAsync(user);

                    response.StatusCode = HttpStatusCode.OK;
                    return response;
                }
                else
                {
                    var service = new TokenService();
                    // Token stripeToken = service.Create(options);
                    if (string.IsNullOrEmpty(user.CustomerStripeId))
                    {
                        var customer = customers.Create(new CustomerCreateOptions
                        {
                            Name = user.Company,
                            Email = user.Email,
                            //Source = stripeToken.Id
                            Source = model.stripeToken
                        });
                        customerId = customer.Id;
                    }
                    var plan = new PlanService().Get(model.planId);
                    var rs = CreateCustomerSubscription(subscriptions, customerId, plan);
                    user.CustomerStripeId = customerId;

                    if (rs.Status == "active")
                    {
                        await _userManager.UpdateAsync(user);
                        response.StatusCode = HttpStatusCode.OK;
                        return response;
                    }
                    else
                    {
                        return response;
                    }



                }
            }
            catch (Exception ex)
            {
                response.StatusCode = HttpStatusCode.InternalServerError;
                response.ErrorMessage = ex.Message;

                error = new LogErrorRequest()
                {
                    ErrorMessage = ex.ToString(),
                    ErrorUrl = "/payment/charge"
                };
                _customLogService.LogError(error);
                return response;
            }
        }

        [HttpPost]
        [Route("cancelsubscription")]
        public void CancelSubscription(string customerId)
        {
            var service = new SubscriptionService();
            var previousList = service.List(new SubscriptionListOptions
            {
                CustomerId = customerId
            });
            if (previousList.Count() > 0)
            {
                var options = new SubscriptionUpdateOptions
                {
                    CancelAtPeriodEnd = true
                };
                service.Update(previousList.FirstOrDefault().Id, options);
            }
        }

        [HttpPost]
        [Route("webhook")]
        public async Task<IActionResult> StripeWebhook()
        {
            LogErrorRequest error;
            try
            {
                var json = new StreamReader(HttpContext.Request.Body).ReadToEnd();
                 
                // validate webhook called by stripe only
                var stripeEvent = EventUtility.ConstructEvent(json, Request.Headers["Stripe-Signature"], "whsec_1FNAhoXXwljPIDSXfDeu6WPKFNUqrHv1");

                if (stripeEvent.Type == "invoice.payment_failed")
                {
                    var subscriptions = new SubscriptionService();
                    var invoice = stripeEvent.Data.Object as Invoice;
                    var previousList = subscriptions.List(new SubscriptionListOptions
                    {
                        CustomerId = invoice.CustomerId
                    });
                    if (previousList.Count() > 0)
                    {
                        var options = new SubscriptionUpdateOptions
                        {
                            CancelAtPeriodEnd = false
                        };
                        subscriptions.Update(previousList.FirstOrDefault().Id, options);
                    }
                    foreach (var item in previousList)
                    {
                        var cancelOptions = new SubscriptionCancelOptions
                        {
                            InvoiceNow = false,
                            Prorate = false,
                        };
                        subscriptions.Cancel(item.Id, cancelOptions);
                    }
                    var userId = _userManager.Users.Where(u => u.CustomerStripeId == invoice.CustomerId).FirstOrDefault().Id;
                    await _clientService.UpdateToFreePlan(userId);
                    await _clientService.ProcessCurrentPricingPlan(userId);
                }
                //if (stripeEvent.Type == "invoice.created")
                //{
                //    var invoice = stripeEvent.Data.Object as Invoice;
                //    var service = new InvoiceService();
                //    var invoicePayOptions = new InvoicePayOptions { };
                //    service.Pay(invoice.Id, invoicePayOptions);
                //}
                if (stripeEvent.Type == "invoice.payment_succeeded")
                {
                    var subService = new SubscriptionService();
                    var invoice = stripeEvent.Data.Object as Invoice;
                    if (!string.IsNullOrEmpty(invoice.ChargeId))
                    {
                        var charge = new ChargeService().Get(invoice.ChargeId);
                        var card = new CardService().Get(invoice.CustomerId, charge.Source.Id);
                        var plan = subService.Get(invoice.Lines.FirstOrDefault().SubscriptionId).Plan;
                        await CreateTransactionInDatabase(invoice, charge, card, plan);
                    }
                    else
                    {
                        var plan = subService.Get(invoice.Lines.FirstOrDefault().SubscriptionId).Plan;
                        var transaction = new Transactions
                        {
                            Amount = Decimal.Divide(invoice.AmountPaid, 100),
                            CreateDate = invoice.WebhooksDeliveredAt ?? DateTime.UtcNow,
                            GlobalPlanId = _planService.GetAllPlans().Where(x => x.AccessId == plan.Id).FirstOrDefault().Id,
                            IsDeleted = false,
                            Status = invoice.Status,
                            UserId = _userManager.Users.Where(u => u.CustomerStripeId == invoice.CustomerId).FirstOrDefault().Id
                        };
                        _stripePaymentService.PostTransaction(transaction);
                    }
                }
                if (stripeEvent.Type == "customer.subscription.deleted")
                {
                    var subscription = stripeEvent.Data.Object as Subscription;
                    var userId = _userManager.Users.Where(u => u.CustomerStripeId == subscription.CustomerId).FirstOrDefault().Id;
                    if (subscription.CancelAtPeriodEnd == true)
                    {
                        await _clientService.UpdateToFreePlan(userId);
                        await _clientService.ProcessCurrentPricingPlan(userId);
                    }
                }
                //if (stripeEvent.Type == "customer.subscription.created")
                //{
                //    var subService = new SubscriptionService();
                //    var subscription = stripeEvent.Data.Object as Subscription;
                //    var plan = subService.Get(subscription.Id).Plan;
                //}
                return Ok();
            }
            catch (StripeException ex)
            {
                error = new LogErrorRequest()
                {
                    ErrorMessage = ex.ToString(),
                    ErrorUrl = "/payment/webhook"
                };
                _customLogService.LogError(error);
                return BadRequest();
            }
            catch (Exception ex)
            {
                error = new LogErrorRequest()
                {
                    ErrorMessage = ex.ToString(),
                    ErrorUrl = "/payment/webhook"
                };
                _customLogService.LogError(error);
                return BadRequest();
            }
        }

        //[HttpPost]
        //[Route("webhook-v2")]
        private IActionResult WebHook()
        {
            LogErrorRequest error;

            var json = new StreamReader(HttpContext.Request.Body).ReadToEnd();

            string secret = "whsec_xdBvhszP6AhOORQtLWWrhZX4VsU9IvMA";
            Invoice invoice;
            try
            {
                var stripeEvent = EventUtility.ConstructEvent(json,
                    Request.Headers["Stripe-Signature"], secret);

                var subscriptionService = new SubscriptionService();
                // Handle the event
                if (stripeEvent.Type == Events.InvoicePaymentSucceeded)
                {
                    invoice = stripeEvent.Data.Object as Invoice;
                    //var plan = subscriptionService.Get(invoice.SubscriptionId).Plan;

                    //var charge = new ChargeService().Get(invoice.ChargeId);
                    //var card = new CardService().Get(invoice.CustomerId, charge.Source.Id);

                    CreateTransactionInDatabase(invoice, null);
                    return Ok(invoice);
                }
                else if (stripeEvent.Type == Events.InvoicePaymentFailed)
                {
                    return Ok($"Ahmad sahab payment FAIL ho gai hai! {Request.Headers["Stripe-Signature"].ToString()}");
                }
                //else if (stripeEvent.Type == Events.PaymentIntentSucceeded)
                //{
                //    var paymentIntent = stripeEvent.Data.Object as PaymentIntent;
                //    //handlePaymentIntentSucceeded(paymentIntent);
                //}
                //else if (stripeEvent.Type == Events.PaymentMethodAttached)
                //{
                //    var paymentMethod = stripeEvent.Data.Object as PaymentMethod;
                //    //handlePaymentMethodAttached(paymentMethod);
                //}
                // ... handle other event types
                else
                {
                    error = new LogErrorRequest()
                    {
                        ErrorMessage = "Invalid event type",
                        ErrorUrl = "/payment/webhook"
                    };

                    _customLogService.LogError(error);
                    // Unexpected event type
                    return BadRequest(json);
                }

            }
            catch (StripeException e)
            {
                error = new LogErrorRequest()
                {
                    ErrorMessage = e.ToString(),
                    ErrorUrl = "/payment/webhook"
                };

                _customLogService.LogError(error);
                return BadRequest(json);
            }
        }


        //Please check the details and let me know what to skipp
        [HttpGet]
        [Route("transactions")]
        public async Task<IActionResult> Transactions()
        {
            return Ok(await _stripePaymentService.GetAllTransactions(UserId));
        }

        #region Local Functions
        private Subscription CreateCustomerSubscription(SubscriptionService subscriptions, string customerId, Plan plan)
        {
            var previousList = subscriptions.List(new SubscriptionListOptions
            {
                CustomerId = customerId
            });
            if (previousList.Count() > 0)
            {
                var options = new SubscriptionUpdateOptions
                {
                    CancelAtPeriodEnd = false
                };
                subscriptions.Update(previousList.FirstOrDefault().Id, options);
            }

            foreach (var item in previousList)
            {
                var cancelOptions = new SubscriptionCancelOptions
                {
                    InvoiceNow = false,
                    Prorate = false,
                };
                subscriptions.Cancel(item.Id, cancelOptions);
            }
            //if (previousList.Count() > 0)
            //{
            //    var items = new List<SubscriptionItemUpdateOption> {
            //        new SubscriptionItemUpdateOption {
            //             PlanId = plan.Id,
            //             Quantity = 1,
            //        },
            //    };
            //    var options = new SubscriptionUpdateOptions
            //    {
            //        Items = items,
            //        Prorate = false
            //    };
            //    subscriptions.Update(previousList.FirstOrDefault().Id, options);
            //    foreach (var item in previousList.FirstOrDefault().Items)
            //    {
            //        var itemService = new SubscriptionItemService();
            //        itemService.Delete(item.Id);
            //    }
            //    var invoiceOptions = new InvoiceCreateOptions
            //    {
            //        CustomerId = customerId,

            //    };
            //    var service = new InvoiceService();
            //    service.Create(invoiceOptions);
            //}
            //else
            //{

            var rs = subscriptions.Create(new SubscriptionCreateOptions
            {
                CustomerId = customerId,
                Items = new List<SubscriptionItemOption>() { new SubscriptionItemOption {
                    PlanId = plan.Id,
                    Quantity = 1,
                } }
               
            });

            return rs;
            // }
            //foreach (var item in previousList)
            //{
            //    var cancelOptions = new SubscriptionCancelOptions
            //    {
            //        InvoiceNow = false,
            //        Prorate = false,
            //    };
            //    subscriptions.Cancel(item.Id, cancelOptions);
            //}
        }

        private async Task CreateTransactionInDatabase(Invoice invoice, Charge charge, Card card, Plan plan)
        {
            var user = _userManager.Users.Where(u => u.CustomerStripeId == invoice.CustomerId).FirstOrDefault();
            _stripePaymentService.PostTransaction(new Transactions
            {
                Amount = Decimal.Divide(invoice.AmountPaid, 100),
                CardName = card.Name != null ? card.Brand : card.Name,
                CreateDate = invoice.WebhooksDeliveredAt ?? DateTime.UtcNow,
                GlobalPlanId = _planService.GetAllPlans().Where(x => x.AccessId == plan.Id).FirstOrDefault().Id,
                IsDeleted = false,
                Receipt = charge.ReceiptUrl ?? string.Empty,
                ReceiptId = charge.ReceiptNumber ?? string.Empty,
                Status = invoice.Status,
                UserId = user.Id
            });
            await _clientService.ProcessCurrentPricingPlan(user.Id);
            var result = _stripePaymentService.SendInvoiceToClient(plan.Nickname, user.Company, invoice.Id, Decimal.Divide(invoice.AmountPaid, 100).ToString(), user.Email);
        }

        private void CreateTransactionInDatabase(Invoice invoice, Plan plan)
        {
            var transaction = new Transactions();

            transaction.Amount = invoice.AmountPaid;
            transaction.CardName = "Test";// invoice.Charge.Source.ToString();
            transaction.CreateDate = invoice.WebhooksDeliveredAt ?? DateTime.UtcNow;
            transaction.GlobalPlanId = 2;//_planService.GetAllPlans().Where(x => x.AccessId == int.Parse(plan.Id)).FirstOrDefault().Id,
            transaction.IsDeleted = false;
            string receiptUrl = string.Empty;
            if (invoice.Charge != null && !string.IsNullOrEmpty(invoice.Charge.ReceiptUrl))
            {
                receiptUrl = invoice.Charge.ReceiptUrl;
            }
            transaction.Receipt = receiptUrl;

            string receiptId = string.Empty;
            if (invoice.Charge != null && !string.IsNullOrEmpty(invoice.Charge.ReceiptNumber))
            {
                receiptId = invoice.Charge.ReceiptNumber;
            }

            transaction.ReceiptId = receiptId;
            transaction.Status = invoice.Status ?? string.Empty;
            transaction.UserId = UserId;

            _stripePaymentService.PostTransaction(transaction);
        }

        #endregion

    }
}